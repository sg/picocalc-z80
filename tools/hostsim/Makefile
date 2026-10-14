# Host build of the PicoCalc machine, for trying BIOS changes without hardware.
Z80PACK ?= ../../submodules/z80pack
CORE = $(Z80PACK)/z80core
IODEV = $(Z80PACK)/iodevices

CFLAGS = -O2 -std=c11 -D_DEFAULT_SOURCE -D_POSIX_C_SOURCE=200809L -I. -I../../srcsim -I$(CORE) -I$(IODEV) -Wall
MODEM = ../../srcsim/modem.c ../../srcsim/net_vars.c hostnet.c $(IODEV)/libtelnet.c
SRCS = main.c hostio.c hostdisks.c ../../srcsim/simmem.c $(MODEM) \
       $(IODEV)/sd-fdc.c $(IODEV)/rtc80.c \
       $(CORE)/sim8080.c $(CORE)/simcore.c $(CORE)/simdis.c $(CORE)/simglb.c \
       $(CORE)/simice.c $(CORE)/simz80.c $(CORE)/simz80-cb.c \
       $(CORE)/simz80-dd.c $(CORE)/simz80-ddcb.c $(CORE)/simz80-ed.c \
       $(CORE)/simz80-fd.c $(CORE)/simz80-fdcb.c

all: hostsim modemtest

hostsim: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

modemtest: modemtest.c $(MODEM)
	$(CC) $(CFLAGS) -o $@ modemtest.c $(MODEM)

clean:
	rm -f hostsim modemtest
.PHONY: all clean
