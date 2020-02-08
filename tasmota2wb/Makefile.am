AM_CPPFLAGS = -I../include/ -g
bin_PROGRAMS = tasmota2wb
tasmota2wb_SOURCES = tasmota2wb.cpp MqttConnection.cpp
tasmota2wb_LDADD =  $(top_builddir)/libs/libwb/libwb.la  \
	$(top_builddir)/libs/libutils/libutils.la \
	-lmosquitto  -lmosquittopp -lpthread
