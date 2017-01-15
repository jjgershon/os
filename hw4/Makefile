KERNELDIR = /usr/src/linux-2.4.18-14custom

include $(KERNELDIR)/.config

CFLAGS = -std=c99 -D__KERNEL__ -DMODULE -I$(KERNELDIR)/include -O -Wall

all: mastermind.o
