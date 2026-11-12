# RTL8192XB 2.4GHz WiFi driver

ccflags-y += -I$(src)

obj-$(CONFIG_RTL8192XB) += rtl8192xb.o

rtl8192xb-y := \
	rtl8192xb_core.o \
	rtl8192xb_hal.o \
	rtl8192xb_phy.o \
	rtl8192xb_reg.o \
	rtl8192xb_debug.o

obj-$(CONFIG_RTL8192XB_DEBUG) += rtl8192xb_debugfs.o
