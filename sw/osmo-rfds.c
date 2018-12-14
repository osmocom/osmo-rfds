/*
 * osmo-rfds-ctrl.c
 *
 * Control software for the Pluto RF delay simulator
 *
 * Copyright (C) 2018 sysmocom GmbH
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include <iio.h>

/*
 	Some info about Pluto use of IIO :

	Device: ad9361-phy	=> Chip control
	------------------

	in_voltage0_[…]: targets RX1
	in_voltage1_[…]: targets RX2 (AD9361 in 2RX2TX mode only)

	out_voltage0_[…]: targets TX1
	out_voltage1_[…]: targets TX2 (AD9361 in 2RX2TX mode only)

	out_altvoltage0_[…]: targets RX LO
	out_altvoltage1_[…]: targets TX LO


	Device: cf-ad9361-lpc	=> RX Path
	---------------------

	voltage 0 -> RX1 I
	voltage 1 -> RX1 Q


	Device: cf-ad9361-dds-core-lpc	=> TX Path + two tone synthesizer
	------------------------------

	voltage 0 -> TX1 I
	voltage 1 -> TX1 Q
	altvoltage[] -> DDS stuff
*/


struct app_options
{
	long long tx_freq;	/* Hz */
	long long rx_freq;	/* Hz */
	float tx_gain;
	float rx_gain;

	long long samp_rate;	/* Hz */

	int buf_cnt;
	int buf_size;

	int   echo_delay;
	float echo_scale;
};

struct app_state
{
	/* Options */
	struct app_options opts;

	/* Pluto device */
	struct {
		struct iio_context *ctx;
		struct iio_device  *phy;

		struct iio_device  *rx;
		struct iio_channel *rx_i;
		struct iio_channel *rx_q;
		struct iio_buffer  *rx_buf;

		struct iio_device  *tx;
		struct iio_channel *tx_i;
		struct iio_channel *tx_q;
		struct iio_buffer  *tx_buf;
	} pluto;
};


static void app_pluto_close(struct app_state *app);

static int
app_pluto_open(struct app_state *app)
{
	/* NULL init */
	memset(&app->pluto, 0x00, sizeof(app->pluto));

	/* Context */
	app->pluto.ctx = iio_create_context_from_uri("local:");
	if (!app->pluto.ctx) {
		fprintf(stderr, "[!] Failed to create IIO context\n");
		goto err;
	}

	/* Devices */
	app->pluto.phy = iio_context_find_device(app->pluto.ctx, "ad9361-phy");
	if (!app->pluto.phy) {
		fprintf(stderr, "[!] Failed to find AD9361 PHY device\n");
		goto err;
	}

	app->pluto.rx = iio_context_find_device(app->pluto.ctx, "cf-ad9361-lpc");
	if (!app->pluto.rx) {
		fprintf(stderr, "[!] Failed to find AD9361 RX device\n");
		goto err;
	}

	app->pluto.tx = iio_context_find_device(app->pluto.ctx, "cf-ad9361-dds-core-lpc");
	if (!app->pluto.tx) {
		fprintf(stderr, "[!] Failed to find AD9361 TX device\n");
		goto err;
	}

	/* Configure RX */
		/* Frequency / Gain / Sampling */
	iio_channel_attr_write_longlong(
		iio_device_find_channel(app->pluto.phy, "altvoltage0", true),
		"frequency",		/* RX LO frequency */
		app->opts.rx_freq
	);

	iio_channel_attr_write_double(
		iio_device_find_channel(app->pluto.phy, "voltage0", false),
		"hardwaregain",		/* RX gain */
		(double)app->opts.rx_gain
	);

	iio_channel_attr_write_longlong(
		iio_device_find_channel(app->pluto.phy, "voltage0", false),
		"sampling_frequency",	/* RX baseband rate */
		app->opts.samp_rate
	);

		/* Channel config */
	app->pluto.rx_i = iio_device_find_channel(app->pluto.rx, "voltage0", false);
	app->pluto.rx_q = iio_device_find_channel(app->pluto.rx, "voltage1", false);

	iio_channel_enable(app->pluto.rx_i);
	iio_channel_enable(app->pluto.rx_q);

	/* Configure TX */
		/* Frequency / Gain / Sampling */
	iio_channel_attr_write_longlong(
		iio_device_find_channel(app->pluto.phy, "altvoltage1", true),
		"frequency",		/* TX LO frequency */
		app->opts.tx_freq
	);

	iio_channel_attr_write_double(
		iio_device_find_channel(app->pluto.phy, "voltage0", true),
		"hardwaregain",		/* TX gain */
		(double)app->opts.tx_gain
	);

	iio_channel_attr_write_longlong(
		iio_device_find_channel(app->pluto.phy, "voltage0", true),
		"sampling_frequency",	/* TX baseband rate */
		app->opts.samp_rate
	);

		/* Channel config */
	app->pluto.tx_i = iio_device_find_channel(app->pluto.tx, "voltage0", true);
	app->pluto.tx_q = iio_device_find_channel(app->pluto.tx, "voltage1", true);

	iio_channel_enable(app->pluto.tx_i);
	iio_channel_enable(app->pluto.tx_q);

	/* Configure the ECHO path */
	uint16_t scale = (uint16_t)(0x4000 * app->opts.echo_scale);
	uint16_t delay = app->opts.echo_delay;
	uint32_t config = (((uint32_t)delay) << 16) | (uint32_t)scale;

	iio_device_reg_write(app->pluto.tx, 0x8000043c, config);	/* I combiner config */
	iio_device_reg_write(app->pluto.tx, 0x8000047c, config);	/* Q combiner config */

	return 0;

err:
	app_pluto_close(app);
	return -1;
}

static void
app_pluto_close(struct app_state *app)
{
	if (app->pluto.rx_i)
		iio_channel_disable(app->pluto.rx_i);

	if (app->pluto.rx_q)
		iio_channel_disable(app->pluto.rx_q);

	if (app->pluto.tx_i)
		iio_channel_disable(app->pluto.tx_i);

	if (app->pluto.tx_q)
		iio_channel_disable(app->pluto.tx_q);

	if (app->pluto.ctx)
		iio_context_destroy(app->pluto.ctx);

	memset(&app->pluto, 0x00, sizeof(app->pluto));
}

static int
app_pluto_start(struct app_state *app)
{
	/* Create buffers and start the streaming */
	iio_device_set_kernel_buffers_count(app->pluto.rx, app->opts.buf_cnt);
	app->pluto.rx_buf = iio_device_create_buffer(app->pluto.rx, app->opts.buf_size, false);
	if (!app->pluto.rx_buf) {
		fprintf(stderr, "[!] Could not create RX buffer");
		return -1;
	}

	iio_device_set_kernel_buffers_count(app->pluto.tx, app->opts.buf_cnt);
	app->pluto.tx_buf = iio_device_create_buffer(app->pluto.tx, app->opts.buf_size, false);
	if (!app->pluto.tx_buf) {
		fprintf(stderr, "[!] Could not create TX buffer");
		return -1;
	}

	/* Echo start */
	iio_device_reg_write(app->pluto.tx, 0x80000418, 0xa);		/* I mux to RF loopback */
	iio_device_reg_write(app->pluto.tx, 0x80000458, 0xa);		/* Q mux to RF loopback */

	return 0;
}

static void
app_pluto_stop(struct app_state *app)
{
	if (app->pluto.rx_buf)
		iio_buffer_destroy(app->pluto.rx_buf);

	if (app->pluto.tx_buf)
		iio_buffer_destroy(app->pluto.tx_buf);

	/* Force zero */
	iio_device_reg_write(app->pluto.tx, 0x80000418, 0);		/* I mux to zero */
	iio_device_reg_write(app->pluto.tx, 0x80000458, 0);		/* Q mux to zero */
}


/* ------------------------------------------------------------------------ */
/* Options                                                                  */
/* ------------------------------------------------------------------------ */

static void
opts_defaults(struct app_options *opts)
{
	memset(opts, 0x00, sizeof(struct app_options));

	opts->tx_freq = 1000000000LL;
	opts->rx_freq = 1000000000LL;
	opts->tx_gain = -40.0f;
	opts->rx_gain =  40.0f ;

	opts->samp_rate = 4000000LL;

	opts->buf_cnt = 4;
	opts->buf_size = 4096;

	opts->echo_scale = 0.25f;
	opts->echo_delay = 50;
}

static void
opts_help(const char *argv0)
{
	fprintf(stderr, "%s [options]\n", argv0);

	fprintf(stderr, " -t, --tx-freq      \n");
	fprintf(stderr, " -r, --rx-freq      \n");
	fprintf(stderr, " -T, --tx-gain      \n");
	fprintf(stderr, " -R, --rx-gain      \n");
	fprintf(stderr, " -s, --samplerate   \n");
	fprintf(stderr, " -c, --buffer-count \n");
	fprintf(stderr, " -b, --buffer-size  \n");
	fprintf(stderr, " -a, --amplitude    \n");
	fprintf(stderr, " -d, --delay        \n");
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
		{ "samplerate",   required_argument, 0, 's' },
		{ "buffer-count", required_argument, 0, 'c' },
		{ "buffer-size",  required_argument, 0, 'b' },
		{ "amplitude",    required_argument, 0, 'a' },
		{ "delay",        required_argument, 0, 'd' },
		{ "help",         no_argument,       0, 'h' },
		{0, 0, 0, 0}
	};
	const char *short_options = "t:r:T:R:s:c:b:a:d:h";

	while (1) {
		int optidx;
		int c = getopt_long (argc, argv, short_options, long_options, &optidx);

		if (c == -1)
			break;

		switch (c) {
		case 't':
			opts->tx_freq = strtoll(optarg, NULL, 10);
			break;

		case 'r':
			opts->rx_freq = strtoll(optarg, NULL, 10);
			break;

		case 'T':
			opts->tx_gain = strtof(optarg, NULL);
			break;

		case 'R':
			opts->rx_gain = strtof(optarg, NULL);
			break;

		case 's':
			opts->samp_rate = strtoll(optarg, NULL, 10);
			break;

		case 'c':
			opts->buf_cnt = strtol(optarg, NULL, 10);
			break;

		case 'b':
			opts->buf_size = strtol(optarg, NULL, 10);
			break;

		case 'a':
			opts->echo_scale = strtof(optarg, NULL);
			break;

		case 'd':
			opts->echo_delay = strtol(optarg, NULL, 10);
			break;

		case 'h':
			opts_help(argv[0]);
			return 1;

		default:
			fprintf(stderr, "Unknown option\n");
			return -1;
		}
	}

	return 0;
}

static void
opts_print(struct app_options *opts, FILE *fd)
{
	fprintf(fd, "[+] Options :\n");

	fprintf(fd, "  . TX frequency   : %.3lf MHz\n", (float)opts->tx_freq / 1e6);
	fprintf(fd, "  . TX gain        : %.1f dB\n",  opts->tx_gain);
	fprintf(fd, "  . RX frequency   : %.3lf MHz\n", (float)opts->rx_freq / 1e6);
	fprintf(fd, "  . RX gain        : %.1f dB\n",  opts->rx_gain);
	fprintf(fd, "\n");

	fprintf(fd, "  . Sample Rate    : %.3lf Msps\n", (float)opts->samp_rate / 1e6);
	fprintf(fd, "\n");

	fprintf(fd, "  . Buffer count   : %d\n", opts->buf_cnt);
	fprintf(fd, "  . Buffer size    : %d bytes\n", opts->buf_size);
	fprintf(fd, "\n");

	fprintf(fd, "  . Echo amplitude : %.1f\n", opts->echo_scale);
	fprintf(fd, "  . Echo delay     : %d samples\n", opts->echo_delay);
	fprintf(fd, "\n");
}


/* ------------------------------------------------------------------------ */
/* Main                                                                     */
/* ------------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
	struct app_state _app, *app=&_app;
	int rv;

	/* Options */
	memset(app, 0x00, sizeof(struct app_state));
	opts_defaults(&app->opts);
	rv = opts_parse(&app->opts, argc, argv);
	if (rv)
		return rv < 0 ? rv : 0;
	opts_print(&app->opts, stderr);

	/* Open and configure pluto */
	rv = app_pluto_open(app);
	if (rv)
		goto err;

	/* Start streaming */
	app_pluto_start(app);

	/* Dummy loop */
	while (1)
	{
		memset(iio_buffer_start(app->pluto.tx_buf), 0x00, app->opts.buf_size);
		iio_buffer_push(app->pluto.tx_buf);
		iio_buffer_refill(app->pluto.rx_buf);
	}

err:
	/* Stop streaming */
	app_pluto_stop(app);

	/* Shutdown */
	app_pluto_close(app);

	return 0;
}
