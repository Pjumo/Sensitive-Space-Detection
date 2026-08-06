obj-m += radar_mock_drv.o

# 공통 헤더(presence_uapi.h) 경로. 실제 프로젝트 구조에 맞게 조정하세요.
ccflags-y += -I$(src)/../../../common/include

# 기본값: Pi 로컬 빌드 (헤더가 apt로 설치되어 있는 경우)
KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# 크로스컴파일 시 make 호출 예:
#   make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
#        KDIR=$(HOME)/pi_bsp/kernel/linux
ARCH ?=
CROSS_COMPILE ?=

.PHONY: all module clean

all: module

module:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
