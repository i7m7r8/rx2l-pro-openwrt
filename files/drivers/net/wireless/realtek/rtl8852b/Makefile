# RTL8852B PHL WiFi driver

ccflags-y += -I$(src)

obj-$(CONFIG_RTL8852B) += rtl8852b.o

rtl8852b-y := \
	rtl8852b_core.o \
	rtl8852b_halmac.o \
	rtl8852b_phl.o \
	rtl8852b_fw.o \
	rtl8852b_reg.o \
	rtl8852b_debug.o

obj-$(CONFIG_RTL8852B_DEBUG) += rtl8852b_debugfs.o
