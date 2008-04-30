lib_LTLIBRARIES = libdres.la

libdres_la_SOURCES = parser.y lexer.l \
                     action.c builtin.c target.c factvar.c variables.c \
                     dres.c
libdres_la_CFLAGS  = @GLIB_CFLAGS@
libdres_la_LIBADD  = @GLIB_LIBS@

INCLUDES = -I$(top_builddir)/include

MAINTAINERCLEANFILES = Makefile.in

clean-local:
	rm -f *~

