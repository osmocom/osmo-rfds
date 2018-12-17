osmo-rfds usage
===============

Flashing image
--------------

Refer to the [official documentation](https://wiki.analog.com/university/tools/pluto/users/firmware).

In short, you can mount the pluto as a mass storage device, copy the
`pluto.frm` file into it at the root, then umount and eject the device.

The led should start blinking quickly, incicating the flash is in progress.
Do **NOT** disconnect the pluto from power while this is in progress.
Wait for it to stop blinking quickly and the new image should be ready to use.


Connect to the pluto
--------------------

Refer to official analog wiki for full details but here are the cliff notes:

* Console:
    * `minicom -D /dev/ttyACM0`

* Network :
    * `ip link set enp0s20u3 up`
    * `ip addr add 192.168.2.2/24 dev enp0s20u3`
    * (replace `enp0s20u3` with whatever name the CDC ECM device was assigned on your machine)
    * `ssh root@192.168.2.1`

* Credentials: root / analog


Running `osmo-rfds`
-------------------

Once connected, the `osmo-rfds` binary can be used to control the loopback.

```
# osmo-rfds -h
osmo-rfds [options]
 -t, --tx-freq
 -r, --rx-freq
 -T, --tx-gain
 -R, --rx-gain
 -s, --samplerate
 -c, --buffer-count
 -b, --buffer-size
 -a, --amplitude
 -d, --delay
 -h, --help
```

Options should be fairly self-explanatory.

 * `tx-freq` / `rx-freq` are the frequencies used, in Hz.
 * `tx-gain` / `rx-gain` are the gain settings in dB. (Refer to iio_info for valid values)
 * `samplerate` is the configured sample rate in Hz. 
   This will influence the bandwidth that's echoed back as well as the delay
   granularity. Minimum sample rate supported by IIO is ~ 2.1 Msps.
 * `buffer-count` / `buffer-size` are advanced IIO parameters which are
   included only for testing and should be left to their default values.
 * `amplitude` is the scaling applied to the received signal before retransmission.
   Beware of overflows !
 * `delay` is the delay to be applied, in number of samples before
   retransmitting the signal. Range is from 0 to 32767


To do a quick test, place the pluto near an UHD device.

Start the pluto relay with RX on 1G and TX on 1.1G with a 1000 samples delay:

```
# osmo-rfds -t 1100000000 -r 1000000000 -d 1000
[+] Options :
  . TX frequency   : 1100.000 MHz
  . TX gain        : -40.0 dB
  . RX frequency   : 1000.000 MHz
  . RX gain        : 40.0 dB

  . Sample Rate    : 4.000 Msps

  . Buffer count   : 4
  . Buffer size    : 4096 bytes

  . Echo amplitude : 0.2
  . Echo delay     : 1000 samples
```


Then start the uhd pinger to Tx on 1G and RX on 1.1G :

```
./pinger -r 1100000000 -t 1000000000
[+] Options :
  . TX frequency      : 1000.000 MHz
  . TX gain           : 60.0 dB
  . RX frequency      : 1100.000 MHz
  . RX gain           : 60.0 dB

  . Master Clock Rate : Auto
  . Sample Rate       : 2.000 Msps

  . Burst length      : 256 samples
  . Burst period      : 250.000 ms
  . Maximum delay     : 5.000 ms

[INFO] [UHD] linux; GNU C++ version 6.4.0; Boost_106500; UHD_3.14.0.0-99-g8e7768c7
[INFO] [B200] Detected Device: B205mini
[INFO] [B200] Operating over USB 3.
[INFO] [B200] Initialize CODEC control...
[INFO] [B200] Initialize Radio control...
[INFO] [B200] Performing register loopback test...
[INFO] [B200] Register loopback test passed
[INFO] [B200] Setting master clock rate selection to 'automatic'.
[INFO] [B200] Asking for clock rate 16.000000 MHz...
[INFO] [B200] Actually got clock rate 16.000000 MHz.
[INFO] [B200] Asking for clock rate 32.000000 MHz...
[INFO] [B200] Actually got clock rate 32.000000 MHz.
[+] Echo at (0.000000, 0.000000, 159487) :
[+] Echo at (0.098540, 615.705994, 549) : 549 (615.705994)
[+] Echo at (0.072381, 1.956668, 583) : 583 (1.956668)
[+] Echo at (0.073429, 2.770400, 583) : 583 (2.770400)
[+] Echo at (0.073496, 2.343132, 583) : 583 (2.343132)
[+] Echo at (0.072222, 2.368830, 583) : 583 (2.368830)
[+] Echo at (0.073371, 3.440041, 583) : 583 (3.440041)
```

This shows a delay of 583 samples.

If we run the pinger with RX on the same frequency as TX we get a 50 samples
delay. So of those 583 samples, only 533 samples are due to the pluto echo.

533 samples at 2 Msps = 266.5 us.
We asked for a delay of 1000 samples at 4 Msps = 250 us.
The additional 16.5 us represent the minimal delay due to analog delays and
fixed processing pipelines in the pluto.

