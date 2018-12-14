/*
 * pinger.cpp
 *
 * Simple 'pinger' to measure RF echo delay
 *
 * Copyright (C) 2018 sysmocom GmbH
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>

#include <pthread.h>

#include <uhd/version.hpp>
#include <uhd/device.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/types/device_addr.hpp>

#include <complex.h>

#define GCC_VERSION (                   \
	__GNUC__ * 10000 +      \
	__GNUC_MINOR__ * 100 +  \
	__GNUC_PATCHLEVEL__     \
)

#if GCC_VERSION >= 40800
# define complex _Complex
# undef _GLIBCXX_HAVE_COMPLEX_H
#endif

extern "C" {
#include <osmocom/dsp/cxvec.h>
#include <osmocom/dsp/cxvec_math.h>
}


struct app_options {
	double tx_freq;		/* Hz */
	double rx_freq;		/* Hz */
	float tx_gain;
	float rx_gain;

	double mcr;		/* Hz */
	double samp_rate;	/* Hz */

	int   burst_len;	/* # samples */
	float burst_period;	/* s */
	float max_delay;	/* s */
};

struct app_burst {
	int len;
	struct osmo_cxvec *cxv;
	int16_t *fxp;
};

struct app_state {
	/* Options */
	struct app_options opts;

	/* Burst data */
	struct app_burst burst;
	struct osmo_cxvec *rxd_cxv;
	struct osmo_cxvec *rxc_cxv;

	/* USRP */
	uhd::usrp::multi_usrp::sptr usrp;
	uhd::tx_streamer::sptr tx;
	uhd::rx_streamer::sptr rx;
	int tx_spp;
	int rx_spp;

	int sps;
	double mcr;
	double samp_rate;

	/* Timing */
	long long ts;
	long long ts_step;
	long long ts_listen;
};


#define POLY ((1 << 15) | (1 << 1) | (1 << 0))

int lfsr_next(uint32_t *state, uint32_t poly)
{
	uint32_t p = *state & poly;

	p ^= (p >> 16);
	p ^= (p >>  8);
	p ^= (p >>  4);
	p ^= (p >>  2);
	p ^= (p >>  1);
	p &= 1;

	*state = (*state << 1) | p;

	return p;
}

static const float rrc_taps[] = {
	 1.3706e-03f, 3.7391e-03f, -5.7944e-03f,  1.0182e-03f,  4.7706e-03f,
	-1.2686e-02f, 1.2766e-02f,  2.8467e-02f, -6.7363e-02f, -4.2208e-02f,
	 3.0290e-01f, 5.4604e-01f,  3.0290e-01f, -4.2208e-02f, -6.7363e-02f,
	 2.8467e-02f, 1.2766e-02f, -1.2686e-02f,  4.7706e-03f,  1.0182e-03f,
	-5.7944e-03f, 3.7391e-03f, 1.3706e-03f
};

static int16_t *
burst_gen(struct app_state *app)
{
	uint32_t lfsr = 1;
	struct osmo_cxvec *burst;
	int len, sps;

	/* Generate a burst of random data */
	len = app->opts.burst_len;
	sps = app->sps;

	burst = osmo_cxvec_alloc(len * sps);
	burst->len = len * sps;

	memset(burst->data, 0x00, sizeof(float complex) * len * sps);

	for (int i=0; i<len; i++)
		burst->data[i*sps] =
			1.0f  * (1 - 2 * lfsr_next(&lfsr, POLY)) +
			1.0fJ * (1 - 2 * lfsr_next(&lfsr, POLY));

	/* If it's SPS > 1, filter it with RRC */
	if (app->sps > 1)
	{
		struct osmo_cxvec *pulse;

		pulse = osmo_cxvec_alloc(sizeof(rrc_taps) / sizeof(float));
		pulse->len = pulse->max_len;
		pulse->flags = CXVEC_FLG_REAL_ONLY;

		for (int i=0; i<pulse->len; i++)
			pulse->data[i] = rrc_taps[i];

		osmo_cxvec_convolve(pulse, burst, CONV_NO_DELAY, burst);

		osmo_cxvec_free(pulse);
	}

	/* Save */
	app->burst.len = len;
	app->burst.cxv = burst;

	osmo_cxvec_dbg_dump(burst, "/tmp/burst.cfile");

	/* Generate fixed point version for fast TX */
	app->burst.fxp = (int16_t*)malloc(sizeof(int16_t) * 2 * burst->len);

	for (int i=0; i<burst->len; i++)
	{
		app->burst.fxp[2*i+0] = (int16_t)(4096.0f * crealf(burst->data[i]));
		app->burst.fxp[2*i+1] = (int16_t)(4096.0f * cimagf(burst->data[i]));
	}

	return NULL;
}

static float
peaks_scan(const struct osmo_cxvec *cv, int *peaks_idx, float *peaks_mag, int N, int win)
{
	float pwr_avg = 0.0f;
	int i, j, k, l;

	/* Pre-init */
	for (i=0; i<N; i++) {
		peaks_idx[i] = -1;
		peaks_mag[i] = 0.0f;
	}

	/* Scan all */
	k = -1;

	for (i=0; i<cv->len; i++)
	{
		/* Magnitude */
		float mag = osmo_normsqf(cv->data[i]);

		pwr_avg += mag;

		/* Worth it ? */
		if (mag < peaks_mag[N-1])
			continue;

		/* Merge ? */
		if ((k >= 0) && ((i - peaks_idx[k]) < win))
		{
			/* Is it worth updating ? */
			if (mag < peaks_mag[k])
				continue;

			/* Move it up if needed */
			l = k;
		}
		else
		{
			l = N-1;
		}

		/* Find insertion point in sorted array and pre-move */
		for (j=l; j>0; j--) {
			if (mag < peaks_mag[j-1])
				break;

			peaks_mag[j] = peaks_mag[j-1];
			peaks_idx[j] = peaks_idx[j-1];
		}

		/* Do the insert */
		peaks_mag[j] = mag;
		peaks_idx[j] = i;
		k = j;
	}

	return pwr_avg / cv->len;
}

static void
burst_find(struct app_state *app, int16_t *buf, int buf_len)
{
	int   peaks_idx[10];
	float peaks_mag[10];
	float pwr;

	/* Alloc buffer */
	if (app->rxd_cxv == NULL) {
		app->rxd_cxv = osmo_cxvec_alloc(buf_len);
		app->rxc_cxv = osmo_cxvec_alloc(buf_len);
	}

	/* Convert to float */
	for (int i=0; i<buf_len; i++)
	{
		app->rxd_cxv->data[i] =
			(1.0f  / 32768.0f) * (float)buf[2*i+0] +
			(1.0fJ / 32768.0f) * (float)buf[2*i+1];
	}

	app->rxd_cxv->len = buf_len;

	/* Correlate */
	osmo_cxvec_correlate(app->burst.cxv, app->rxd_cxv, 1, app->rxc_cxv);

	/* Peak finding */
	pwr = peaks_scan(app->rxc_cxv, peaks_idx, peaks_mag, 10, 25);

	/* Display results */
	fprintf(stderr, "[+] Echo at : ");
	for (int i=0; i<10; i++)
		if ((peaks_mag[i] > (pwr * 25.0f)) &&
		    (peaks_mag[i] > (peaks_mag[0] / 10.0f)) &&
		    (peaks_idx[i] > 0))
			fprintf(stderr, "%s%d (%f)", i ? ", " : "", peaks_idx[i], peaks_mag[i]);
		else
			break;
	fprintf(stderr, "\n");
}

static void
burst_free(struct app_state *app)
{
	osmo_cxvec_free(app->burst.cxv);
	free(app->burst.fxp);
}


static int
dev_open(struct app_state *app)
{
	/* Device */
	std::string args = "";

	if (app->opts.mcr > 0)
		args += "master_clock_rate=" + std::to_string(app->opts.mcr);

	uhd::device_addr_t addr(args);
	uhd::device_addrs_t dev_addrs = uhd::device::find(addr);

	app->usrp = uhd::usrp::multi_usrp::make(addr);

	/* TX setup */
	uhd::stream_args_t tx_stream_args("sc16");
	tx_stream_args.args["send_frame_size"] = addr.get("send_frame_size", "4096");
	tx_stream_args.args["num_send_frames"] = addr.get("num_send_frames", "1024");

	app->usrp->set_tx_rate(app->opts.samp_rate);
	app->usrp->set_tx_freq(app->opts.tx_freq);
	app->usrp->set_tx_gain(app->opts.tx_gain);

	app->tx = app->usrp->get_tx_stream(tx_stream_args);
	app->tx_spp = app->tx->get_max_num_samps();

	/* RX setup */
	uhd::stream_args_t rx_stream_args("sc16");
	rx_stream_args.args["recv_frame_size"] = addr.get("recv_frame_size", "4096");
	rx_stream_args.args["num_recv_frames"] = addr.get("num_recv_frames", "1024");

	app->usrp->set_rx_rate(app->opts.samp_rate);
	app->usrp->set_rx_freq(app->opts.rx_freq);
	app->usrp->set_rx_gain(app->opts.rx_gain);

	app->rx = app->usrp->get_rx_stream(rx_stream_args);
	app->rx_spp = app->rx->get_max_num_samps();

	/* Get actual values for the app to adapt */
	app->mcr = app->usrp->get_master_clock_rate();
	app->samp_rate = app->usrp->get_rx_rate();

	/* In 1 SPS mode, use ADI to apply a RRC */
	if (app->sps == 1) {
		/* This filter assumes the ADI FIR is 4:1 ratio but that will be the case
		 * for low sample rates */
		static boost::int16_t fir_coeffs[]={
			// ADI taps (from UMTS.ftr)
			   -9,    -5,    -8,    12,    28,    50,    49,    31,
			   -7,   -37,   -43,   -15,    28,    58,    46,    -1,
			  -52,   -64,   -25,    40,    79,    53,   -26,   -97,
			  -96,    -9,   106,   154,    81,   -75,  -198,  -180,
			  -13,   186,   257,   126,  -121,  -284,  -213,    60,
			  311,   297,   -28,  -423,  -516,  -111,   577,   998,
			  655,  -436, -1591, -1827,  -593,  1633,  3390,  3064,
			   90, -4269, -7192, -5700,  1544, 13088, 24996, 32595,
			32767, 25516, 13944,  2648, -4542, -6239, -3737,   133,
			 2741,  2952,  1332,  -630, -1642, -1341,  -284,   638,
			  860,   430,  -172,  -470,  -323,    48,   302,   257,
			   -3,  -238,  -261,   -74,   156,   252,   154,   -44,
			 -186,  -178,   -47,    96,   146,    84,   -27,   -98,
			  -83,    -8,    62,    74,    27,   -37,   -66,   -44,
			    9,    51,    55,    21,   -21,   -45,   -33,    -1,
			   34,    48,    44,    21,     5,   -11,    -7,    -9,
		};

		std::string tx_filter_path = "/mboards/0/dboards/A/tx_frontends/A/filters/FIR_1";
		std::string rx_filter_path = "/mboards/0/dboards/A/rx_frontends/A/filters/FIR_1";

		for (int i=0; i<2; i++)
		{
			std::string filter_path = i ? rx_filter_path : tx_filter_path;
			uhd::filter_info_base::sptr filter = app->usrp->get_filter(filter_path);

			uhd::digital_filter_fir<boost::int16_t>::sptr fir_filter =
				boost::dynamic_pointer_cast<
				uhd::digital_filter_fir<boost::int16_t> >(filter);
			std::vector<boost::int16_t> taps_vect;

			taps_vect.assign(fir_coeffs, fir_coeffs+128);
			fir_filter->set_taps(taps_vect);
			app->usrp->set_filter(filter_path, filter);
		}
	}

	return 0;
}

static void *
rx_thread_fn(void *arg)
{
	struct app_state *app = (struct app_state *)arg;
	long long ts = app->ts;

	int bl;
	int16_t *buf;
	std::vector<int16_t *> buff_ptrs;

	/* Buffer */
	bl = app->ts_listen;
	buf = (int16_t*) malloc(sizeof(int16_t) * 2 * bl);
	buff_ptrs.push_back(buf);

	/* Infinite loop */
	while (1)
	{
		/* Start streaming */
		uhd::stream_cmd_t stream_cmd(uhd::stream_cmd_t::STREAM_MODE_NUM_SAMPS_AND_DONE);
		stream_cmd.num_samps = bl;
		stream_cmd.stream_now = false;
		stream_cmd.time_spec = uhd::time_spec_t::from_ticks(ts, app->mcr);
		app->rx->issue_stream_cmd(stream_cmd);

		/* Process buffer */
		burst_find(app, buf, bl);

		/* Receive loop */
		for (int ofs=0; ofs<bl; )
		{
			std::vector<int16_t *> buff_ptrs;
			uhd::rx_metadata_t md;

			buff_ptrs.push_back(&buf[ofs]);

			size_t num_rx_samps = app->rx->recv(
				buff_ptrs, bl-ofs, md, app->opts.burst_period * 2.0f, false
			);
			ofs += num_rx_samps;
			if (num_rx_samps == 0)
				printf("RX stall\n");
		}

		/* Next expected */
		ts += app->ts_step;
	}

	return NULL;
}

static void *
tx_thread_fn(void *arg)
{
	struct app_state *app = (struct app_state *)arg;
	long long ts = app->ts;
	int pending = 0;

	while (1)
	{
		uhd::tx_metadata_t md;
		int rv, bl;

		/* Send burst if no too many are pending */
		if (pending < 2)
		{
			/* Try to send burst */
			md.has_time_spec  = true;
			md.start_of_burst = true;
			md.end_of_burst   = true;
			md.time_spec      = uhd::time_spec_t::from_ticks(ts, app->mcr);

			bl = app->burst.len;

			rv = app->tx->send(app->burst.fxp, bl, md, 0.1f);
			if (rv != bl)
				fprintf(stderr, "[!] TX rv: %d\n", rv);

			ts += app->ts_step;
			pending++;
		}

		/* Get message / acks */
		uhd::async_metadata_t amd;
		if (app->tx->recv_async_msg(amd, 0.1f))
		{
			if (amd.event_code == uhd::async_metadata_t::event_code_t::EVENT_CODE_BURST_ACK)
				pending--;
			else
				fprintf(stderr, "[!] TX async error\n");
		}
	}

	return NULL;
}


static void
opts_defaults(struct app_options *opts)
{
	opts->tx_freq = 1e9;		/* 1 GHz */
	opts->rx_freq = 1e9;		/* 1 GHz */
	opts->tx_gain = 60.0f;
	opts->rx_gain = 60.0f;

	opts->mcr = 0.0;		/* Default */
	opts->samp_rate = 2e6;		/* 2 Msps */

	opts->burst_len = 256;		/* 128 us */
	opts->burst_period = 250e-3;	/* 250 ms */
	opts->max_delay = 5e-3;		/*   5 ms */
}

static void
opts_help(const char *argv0)
{
	fprintf(stderr, "%s [options]\n", argv0);

	fprintf(stderr, " -t, --tx-freq      \n");
	fprintf(stderr, " -r, --rx-freq      \n");
	fprintf(stderr, " -T, --tx-gain      \n");
	fprintf(stderr, " -R, --rx-gain      \n");
	fprintf(stderr, " -m, --mcr          \n");
	fprintf(stderr, " -s, --samplerate   \n");
	fprintf(stderr, " -l, --burst-len    \n");
	fprintf(stderr, " -p, --burst-period \n");
	fprintf(stderr, " -m, --max-delay    \n");
	fprintf(stderr, " -h, --help         \n");
}

static int
opts_parse(struct app_options *opts, int argc, char *argv[])
{
	const struct option long_options[] =
	{
		{ "tx-freq",      required_argument, 0, 't' },
		{ "rx-freq",      required_argument, 0, 'r' },
		{ "tx-gain",      required_argument, 0, 'T' },
		{ "rx-gain",      required_argument, 0, 'R' },
		{ "mcr",          required_argument, 0, 'm' },
		{ "samplerate",   required_argument, 0, 's' },
		{ "burst-len",    required_argument, 0, 'l' },
		{ "burst-period", required_argument, 0, 'p' },
		{ "max-delay",    required_argument, 0, 'd' },
		{ "help",       no_argument,       0, 'h' },
		{0, 0, 0, 0}
	};
	const char *short_options = "t:r:T:R:m:s:l:p:d:h";

	while (1) {
		int optidx;
		int c = getopt_long (argc, argv, short_options, long_options, &optidx);

		if (c == -1)
			break;

		switch (c) {
		case 't':
			opts->tx_freq = strtod(optarg, NULL);
			break;

		case 'r':
			opts->rx_freq = strtod(optarg, NULL);
			break;

		case 'T':
			opts->tx_gain = strtof(optarg, NULL);
			break;

		case 'R':
			opts->rx_gain = strtof(optarg, NULL);
			break;

		case 'm':
			opts->mcr = strtod(optarg, NULL);
			break;

		case 's':
			opts->samp_rate = strtod(optarg, NULL);
			break;

		case 'l':
			opts->burst_len = strtol(optarg, NULL, 10);
			break;

		case 'p':
			opts->burst_period = strtof(optarg, NULL);
			break;

		case 'd':
			opts->max_delay = strtof(optarg, NULL);
			break;

		case 'h':
			opts_help(argv[0]);
			return 1;

		default:
			fprintf(stderr, "Unknown option\n");
			return -1;
		};
	}

	return 0;
}

static void
opts_print(struct app_options *opts, FILE *fd)
{
	fprintf(fd, "[+] Options :\n");

	fprintf(fd, "  . TX frequency      : %.3lf MHz\n", opts->tx_freq / 1e6);
	fprintf(fd, "  . TX gain           : %.1f dB\n",  opts->tx_gain);
	fprintf(fd, "  . RX frequency      : %.3lf MHz\n", opts->rx_freq / 1e6);
	fprintf(fd, "  . RX gain           : %.1f dB\n",  opts->rx_gain);
	fprintf(fd, "\n");

	if (opts->mcr > 0.0)
		fprintf(fd, "  . Master Clock Rate : %.3lf MHz\n", opts->mcr / 1e6);
	else
		fprintf(fd, "  . Master Clock Rate : Auto\n");
	fprintf(fd, "  . Sample Rate       : %.3lf Msps\n", opts->samp_rate / 1e6);
	fprintf(fd, "\n");

	fprintf(fd, "  . Burst length      : %d samples\n", opts->burst_len);
	fprintf(fd, "  . Burst period      : %.3f ms\n", 1e3f * opts->burst_period);
	fprintf(fd, "  . Maximum delay     : %.3f ms\n", 1e3f * opts->max_delay);
	fprintf(fd, "\n");
}


int main(int argc, char *argv[])
{
	struct app_state _app, *app = &_app;
	pthread_t tx_thread, rx_thread;
	int rv;

	/* Options */
	memset(app, 0x00, sizeof(struct app_state));
	opts_defaults(&app->opts);
	rv = opts_parse(&app->opts, argc, argv);
	if (rv)
		return rv < 0 ? rv : 0;
	opts_print(&app->opts, stderr);

	/* Operation mode */
	if ((app->opts.samp_rate == app->opts.mcr) && (app->opts.samp_rate <= 10e6))
		app->sps = 1;
	else
		app->sps = 2;

	/* Open device */
	rv = dev_open(app);
	if (rv)
		return -1;

	/* Generate burst */
	burst_gen(app);

	/* Get initial time */
	uhd::time_spec_t now = app->usrp->get_time_now();
	app->ts_step   = (long long)(app->mcr * app->opts.burst_period);
	app->ts_listen = (long long)(app->mcr * app->opts.max_delay);
	app->ts = now.to_ticks(app->mcr);
	app->ts += app->ts_step;

	/* Start threads */
	pthread_create(&tx_thread, NULL, tx_thread_fn, app);
	pthread_create(&rx_thread, NULL, rx_thread_fn, app);

	/* Wait for completion */
        pthread_join(tx_thread, NULL);

        pthread_cancel(rx_thread);
        pthread_join(rx_thread, NULL);

	/* Cleanup */
	burst_free(app);

	return 0;
}
