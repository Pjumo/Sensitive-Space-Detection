obj-m += pir_driver.o

ccflags-y += -I$(src)/../../../common/include

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
DTC ?= dtc

.PHONY: all module overlay clean

all: module overlay

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

overlay:
	$(DTC) -@ -I dts -O dtb \
		-o pir-presence.dtbo \
		pir-presence-overlay.dts

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	$(RM) pir-presence.dtbo
