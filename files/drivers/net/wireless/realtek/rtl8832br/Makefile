# RTL8832BR 5GHz WiFi driver

ccflags-y += -I$(src)

obj-$(CONFIG_RTL8832BR) += rtl8832br.o

rtl8832br-y := \
	rtl8832br_core.o \
	rtl8832br_iqk.o \
	rtl8832br_tssi.o \
	rtl8832br_txgapk.o \
	rtl8832br_psd.o \
	rtl8832br_power.o \
	rtl8832br_reg.o \
	rtl8832br_rf.o \
	rtl8832br_debug.o

obj-$(CONFIG_RTL8832BR_DEBUG) += rtl8832br_debugfs.o
