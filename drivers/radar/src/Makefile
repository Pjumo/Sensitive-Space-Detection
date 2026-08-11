obj-m += radar_mock_drv.o

# presence_uapi.h가 같은 디렉토리(drivers/radar/mock/)에 있으므로
# 별도 -I 경로 지정 불필요 (큰따옴표 include는 소스 디렉토리를 기본 탐색)

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
