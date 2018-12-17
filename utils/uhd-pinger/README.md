uhd-pinger
==========

This utility uses a UHD device with timestamp support to measure the latency
between transmitting a pulse and receiving it back.

You can measure the apparent latency of the device itself (internal processing,
analog delays, ...) by using a loopback. Or you can also measure the latency
of an external device. Make sure to first make a measure of the UHD device
own latency and substract that from you measurement to get the real latency
of the DUT.

This latency is a function of the sample rate and the master clock rate, so
make sure to set those parameters correctly !


How it works
------------

This repeatadely transmits a burst made of QPSK symbol, filtered by a simple
RRC.

Ideally instead of a random LFSR, this should use codes that have an 
auto-correlation as close as possible to the delta function, but this is
not implemented yet.

At the same time a transmit burst is sent, the UHD device is asked to receive
some samples at the same timestamp. Then in those receive samples, we
look for the same burst we transmitted (using correlation) and display the
position (in samples) that this was found in.

The correlation can have false 'echos' and sometimes needs to be interpreted
rather than used as raw data directly.

The receive window is limited to a much smaller length than the pulse period
to make sure the host has time to receive the data, correlate it before sending
the next pulse. (This isn't especially optimized ATM).

If you see 'RX Stall' errors, try increasing the burst period, or diminishing
the receive window ('Max delay').


Example usage
-------------

Starting the utility with default options should show something like :

```
$ ./pinger
[+] Options :
  . TX frequency      : 1000.000 MHz
  . TX gain           : 60.0 dB
  . RX frequency      : 1000.000 MHz
  . RX gain           : 60.0 dB

  . Master Clock Rate : Auto
  . Sample Rate       : 2.000 Msps

  . Burst length      : 256 samples
  . Burst period      : 250.000 ms
  . Maximum delay     : 5.000 ms
```

This means that it will transmit a 256 sample long pulse every 250 ms. The
receive window is 5 ms..

Typical output would be something like : 

```
[INFO] [UHD] linux; GNU C++ version 6.4.0; Boost_106500; UHD_3.14.0.0-99-g8e7768c7
[INFO] [B200] Loading firmware image: /opt/gnuradio-37qt5/share/uhd/images/usrp_b200_fw.hex...
[INFO] [B200] Detected Device: B205mini
[INFO] [B200] Loading FPGA image: /opt/gnuradio-37qt5/share/uhd/images/usrp_b205mini_fpga.bin...
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
[+] Echo at :
[+] Echo at : 15 (71.541344)
[+] Echo at : 50 (1.466832)
[+] Echo at : 50 (1.351750)
[+] Echo at : 50 (1.278904)
[+] Echo at : 50 (1.423250)
[+] Echo at : 50 (1.461555)
[+] Echo at : 50 (1.621279)
[+] Echo at : 50 (1.626796)
```

This means that for those parameters, we see our TX pulse echoed back on
RX 50 samples later.

Obviously this isn't actual RF delay, this is just a mismatch of UHD TX and RX
timestamps in this case.
