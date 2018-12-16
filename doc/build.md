Building Image
==============

Building the image is pretty well documented on ADI's wiki about the pluto :

* [Obtaining the sources](https://wiki.analog.com/university/tools/pluto/obtaining_the_sources)
* [Building the image](https://wiki.analog.com/university/tools/pluto/building_the_image)

So this document will only describe the broad lines and deviations from the process described in those links.


Environment
-----------

Xilinx Vivado 2017.4.1 is required to build the image. We will also use the Vivado hardfloat toolchain rather than the default one (documentation for this can be found in the `README.md` of the `plutosdr-fw` repository).

Required environment variables :

```
export CROSS_COMPILE=arm-linux-gnueabihf-
export PATH=$PATH:/opt/Xilinx/SDK/2017.4/gnu/aarch32/lin/gcc-arm-linux-gnueabi/bin/
export VIVADO_SETTINGS=/opt/Xilinx/Vivado/2017.4/settings64.sh
```

**DO NOT** source the vivado settings ( `settings64.sh` ) directly into your shell. This will break the build ! The build process will use the environment variable `VIVADO_SETTINGS` to manually setup the required envirtonment whenever it's needed.

The build process also needs X running (because Xilinx's SDK needs X). But no interaction is needed, so running Xvfb in the background and manually setting up `DISPLAY` works fine to build on headless servers.

```
Xvfb &
export DISPLAY=:0
```


Prepare Sources
---------------

First step is to clone them from ADI :

```
git clone --recursive https://github.com/analogdevicesinc/plutosdr-fw.git
cd plutosdr-fw
```

Then apply the patches in this repository to the appropriate submodules :

* `patch-hdl-add-signal-delay-datapath.diff` : This is the main patch add custom logic in the datapath of the FPGA
* `patch-buildroot-vivado-crosscompiler.diff`: This updates the buildroot configuration to be compatible with the _arm-linux-gnueabihf_ toolchain.
* `patch-buildroot-use-ecm-instead-of-rndis.diff` : This replaces the propriatary RNDIS mode with the standard CDC ECM mode for network gadget.
* `patch-buildroot-add-ssh-key-example.diff` : Example patch to add SSH key directly into the image for easy access


Finally you can copy the latest version of the `sig_combine.v` and `sig_delay.v` files from this repository (in `gw/`) to `plutosdr-fw/hdl/library/common/`.


Build
-----

If everything above went well, just type `make` in the `plutosdr-fw` directory and it will build everything ...



Building Control Software
=========================

To build the control software, you need the 'SDK' / 'SYSROOT' that will be built as part of building the image above. It can be found under `plutosdr-fw/buildroot/output/host/arm-buildroot-linux-gnueabihf/sysroot/`.

Then in the `sw` directory of this repository, simply type `make` while specifying the sysroot to use:

```
make SYSROOT=xxx/plutosdr-fw/buildroot/output/host/arm-buildroot-linux-gnueabihf/sysroot/
```

This should result in a `osmo-rfds` binary.

To include it directly on the image, you can copy it to `plutosdr-fw/buildroot/output/target/usr/sbin/` and run the `make` in `plutosdr-fw` again to update the image.
