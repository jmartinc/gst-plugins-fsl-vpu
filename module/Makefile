ifeq ($(PTX_KERNEL_DIR),)

all:
	@echo "please export PTX_KERNEL_DIR"

clean:
	$(MAKE) -C $(PTX_KERNEL_DIR) M=$(PWD) V=$(V) clean

else

PWD	:= $(shell pwd)
obj-m += imx-vpu.o

modules modules_install clean:
	$(MAKE) -C $(PTX_KERNEL_DIR) M=$(PWD) V=$(V) $@

endif

