
if HAVE_CHECK

TESTS = check_stomp \
	check_frame

noinst_PROGRAMS = check_stomp \
		  check_frame

check_PROGRAMS = check_stomp\
		 check_frame

check_stomp_SOURCES = check_stomp.c \
		      $(top_builddir)/src/stomp.h 

check_stomp_CFLAGS = @CHECK_CFLAGS@ -Wall
check_stomp_LDADD = $(top_builddir)/src/libstomp.la @CHECK_LIBS@

check_frame_SOURCES = check_frame.c \
		      $(top_builddir)/src/frame.h \
		      $(top_builddir)/src/frame.c \
		      $(top_builddir)/src/hdr.h \
		      $(top_builddir)/src/hdr.c

check_frame_CFLAGS = @CHECK_CFLAGS@ -g -Wall -Werror
check_frame_LDADD = @CHECK_LIBS@

endif
