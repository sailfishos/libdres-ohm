noinst_PROGRAMS = dres-test

dres_test_SOURCES = dres-test.c
dres_test_CFLAGS  = @LIBPROLOG_CFLAGS@    \
                    @LIBFACT_CFLAGS@      \
                    @LIBFACTMAP_CFLAGS@   \
                    @LIBRELATION_CFLAGS@  \
                    @SWIPROLOG_CFLAGS@    \
                    @GLIB_CFLAGS@

dres_test_LDADD   = ../src/libdres.la     \
                    @LIBPROLOG_LIBS@      \
                    @LIBFACT_LIBS@        \
                    @LIBFACTMAP_LIBS@     \
                    @LIBRELATION_LIBS@    \
                    @SWIPROLOG_LDFLAGS@   \
                    @GLIB_LIBS@ @LEXLIB@

INCLUDES = -I$(top_builddir)/include
