# Note: plugindir is set in configure

AM_CFLAGS = -I$(top_srcdir)/src/libamlimageplayer \
						-I$(STAGING_DIR)/usr/include \
						-I$(STAGING_DIR)/usr/include/libdrm

## libamlimageplayer.a

lib_LIBRARIES = libamlimageplayer.a

libamlimageplayer_a_SOURCES = libamlimageplayer/imageplayer.c \
															libamlimageplayer/netConnectionUtil.c


## Plugin 3 (aml picture decode sink)

plugin_LTLIBRARIES = libgstamlpicsink.la


libgstamlpicsink_la_DEPENDENCIES = libamlimageplayer.a

libgstamlpicsink_la_SOURCES = gstamlpicsink.c

libgstamlpicsink_la_CFLAGS = $(GST_CFLAGS) $(AM_CFLAGS)
libgstamlpicsink_la_LIBADD = libamlimageplayer.a $(GST_LIBS)
libgstamlpicsink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstamlpicsink_la_LIBTOOLFLAGS = --tag=disable-static
libgstamlpicsink_la_LIBADD += -L$(TARGET_DIR)/usr/lib -ldrm -lpthread
include_HEADERS = gstamlpicsink.h

