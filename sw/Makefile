CROSS ?= arm-linux-gnueabihf-

CC=$(CROSS)gcc
LD=$(CROSS)gcc

CFLAGS=-Wall --sysroot=$(SYSROOT)
LDLIBS=-liio --sysroot=$(SYSROOT)


all: sysroot_test osmo-rfds

osmo-rfds:  osmo-rfds.o

sysroot_test:
	@if [ "x$(SYSROOT)" = "x" ]; then \
		echo >&2 "[!] Need SYSROOT to be pointing to the buildroot SDK"; \
		false; \
	fi

clean:
	rm -f osmo-rfds *.o

.PHONY: sysroot_test clean
