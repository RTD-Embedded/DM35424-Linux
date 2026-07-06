#
#	FILE NAME: Makefile
#
#	FILE DESCRIPTION: Make description file for building driver
#
#	PROJECT NAME: Linux Software (DM35424)
#
#	PROJECT VERSION: (Defined in README.TXT)
#
#	This file and its contents are copyright (C) RTD Embedded Technologies,
#	Inc.  All Rights Reserved.
#
#	This software is licensed as described in the RTD End-User Software
#	License Agreement.  For a copy of this agreement, refer to the file
#	LICENSE.TXT (which should be included with this software) or contact RTD
#	Embedded Technologies, Inc.
#

MY_CFLAGS = -I$(shell pwd)/../include -g 


#
# Kernel build environment directory.  Supposedly it is safer to use this
# method of referring to it than using /usr/src.
#

KERNEL_DIR := /lib/modules/$(shell uname -r)/build

KERNEL_VER := $(shell uname -r | cut -c1-3)

ifeq (4.0, $(firstword $(shell printf '%s\n' $(KERNEL_VER) 4.0 | sort -V)))
SUBDIRS_OR_M := M
else
SUBDIRS_OR_M := SUBDIRS
endif


# C FLAGS
ifeq (6.0, $(firstword $(shell printf '%s\n' $(KERNEL_VER) 6.0 | sort -V)))
EFLAGS = ccflags-y="$(MY_CFLAGS)"
else
EFLAGS = EXTRA_CFLAGS="$(MY_CFLAGS)"
endif


obj-m := rtd-dm35424.o

DRIVER_MODULE=rtd-dm35424.ko
FILES_TO_CLEAN= \
	*.o \
	rtd-dm35424.ko \
	.*.cmd \
	*.ko \
	.tmp_versions \
	rtd-dm35424.mod.c \
	rtd-dm35424.mod \
	.*.d \
	.*.o \
	*.symvers \
	*.order \
    *.markers \
	*~

.PHONY: debug debug_int debug_dma debug_all driver

driver:		rtd-dm35424.c
	
	make -C $(KERNEL_DIR) $(SUBDIRS_OR_M)=`pwd` $(EFLAGS) modules
	
debug: MY_CFLAGS += -DDEBUG
debug: driver

debug_int: MY_CFLAGS += -DDEBUG_INT
debug_int: driver

debug_dma: MY_CFLAGS += -DDEBUG_DMA
debug_dma: driver

debug_all: MY_CFLAGS += -DDEBUG_ALL
debug_all: driver

clean:
	rm -rf $(FILES_TO_CLEAN)

load:
	insmod ./$(DRIVER_MODULE)
	@sleep 1
	chmod 666 /dev/rtd-dm35424-*

unload:
	rmmod rtd-dm35424
	@echo "Removing old device files ..."; \
	rm -f /dev/rtd-dm35424-* \
	
