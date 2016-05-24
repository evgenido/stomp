AM_CPPFLAGS = -Wall -Werror
lib_LTLIBRARIES = libstomp.la
libstomp_la_SOURCES = \
		      stomp.c \
		      frame.c \
		      hdr.c

libstomp_la_CFLAGS = -I./include
libstomp_la_LDFLAGS = -version-info $(STOMP_SO_VERSION) -lrt
stomp_includedir = $(includedir)/stomp
stomp_include_HEADERS = include/stomp/stomp.h include/stomp/frame.h
pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = ../stomp.pc
