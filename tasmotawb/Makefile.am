AM_CPPFLAGS = -I../include/ -g
bin_PROGRAMS = tasmotawb
tasmotawb_SOURCES = tasmotawb.cpp MqttConnection.cpp
tasmotawb_LDADD =  $(top_builddir)/libs/libcomm/libcomm.la \
	$(top_builddir)/libs/libwb/libwb.la  \
	$(top_builddir)/libs/libutils/libutils.la \
	-lmosquitto  -lmosquittopp -lpthread
