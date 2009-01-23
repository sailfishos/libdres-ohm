pkglib_LTLIBRARIES = libohm_dres.la
pkglibdir          = @OHM_PLUGIN_DIR@
EXTRA_DIST         = $(config_DATA)
configdir          = $(sysconfdir)/ohm/plugins.d
config_DATA        = dres.ini

libohm_dres_la_SOURCES = resolver.c
#libohm_dres_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ @LIBPROLOG_CFLAGS@
#libohm_dres_la_LIBADD = @OHM_PLUGIN_LIBS@ @LIBPROLOG_LIBS@ \
#                        ../src/libdres.la
libohm_dres_la_CFLAGS = @OHM_PLUGIN_CFLAGS@ -fno-strict-aliasing
libohm_dres_la_LIBADD = @OHM_PLUGIN_LIBS@ ../src/libdres.la
libohm_dres_la_LDFLAGS = -module -avoid-version

INCLUDES = -I$(top_builddir)/include

MAINTAINERCLEANFILES = Makefile.in

clean-local:
	rm -f *~
