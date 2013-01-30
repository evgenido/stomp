AM_CPPFLAGS = -Wall -Werror
lib_LTLIBRARIES = libstomp.la
libstomp_la_SOURCES = \
		      stomp.c \
		      stomp.h \
		      frame.c \
		      frame.h \
		      hdr.c \
		      hdr.h

libstomp_la_LDFLAGS = -version-info $(STOMP_SO_VERSION) -lrt
stomp_includedir = $(includedir)/stomp
stomp_include_HEADERS = stomp.h 
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(top_builddir)/stomp.pc
