osmo-rfds - RF delay simulator
==============================

This repository contains the source code for an RF delay simulator,
which can be used to artificially delay RF signals by a configurable
amount of [variable] delay.  This is useful to test timing control
loops in TDMA systems, such as GSM.

It is part of the [Osmocom](https://osmocom.org/) Open Source Mobile
Communications project.

The hardware we use for this is the PlotoSDR, also known as
[ADALM-PLUTO](https://www.analog.com/en/design-center/evaluation-hardware-and-software/evaluation-boards-kits/adalm-pluto.html).

This repository contains the FPGA gateware, firmware and software to
turn the PlutoSDR into said RF delay simulator.

Homepage
--------

The official homepage of the project is
https://osmocom.org/projects/osmo-rfds/wiki/Wiki

GIT Repository
--------------

You can clone from the official osmo-rfds.git repository using

	git clone https://gitea.osmocom.org/sdr/osmo-rfds.git

There is also a web interface at https://gitea.osmocom.org/sdr/osmo-rfds

Documentation
-------------

See the doc/ sub-directory.

Video Presentation
------------------

A recorded video presentation about osmo-rfds can be found at https://media.ccc.de/v/osmodevcon2019-101-osmo-rfds-osmocom-rf-delay-simulator

Mailing List
------------

Discussions related to osmo-rfds are happening on the
openbsc@lists.osmocom.org mailing list, please see
https://lists.osmocom.org/mailman/listinfo/openbsc for subscription
options and the list archive.

Please observe the [Osmocom Mailing List
Rules](https://osmocom.org/projects/cellular-infrastructure/wiki/Mailing_List_Rules)
when posting.
