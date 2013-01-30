lib_LTLIBRARIES = libstomp-@STOMP_API_VERSION@.la
libstomp_@STOMP_API_VERSION@_la_SOURCES = stomp.c \
	stomp.h \
	frame.c \
	frame.h \
	hdr.c \
	hdr.h

libstomp_@STOMP_API_VERSION@_la_CFLAGS = -Wall -Werror 
libstomp_@STOMP_API_VERSION@_la_LDFLAGS = -version-info $(STOMP_SO_VERSION) -lrt
stomp_includedir = $(includedir)/stomp-$(STOMP_API_VERSION)
stomp_include_HEADERS = stomp.h 
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(top_builddir)/stomp.pc
