lib_LTLIBRARIES = libdres.la

libdres_la_SOURCES = parser.y lexer.l \
                     action.c builtin.c target.c literal.c \
                     factvar.c dresvar.c variables.c scope.c \
                     prereq.c dres.c
libdres_la_CFLAGS  = @GLIB_CFLAGS@
libdres_la_LIBADD  = @GLIB_LIBS@

INCLUDES = -I$(top_builddir)/include

MAINTAINERCLEANFILES = Makefile.in

clean-local:
	rm -f *~

