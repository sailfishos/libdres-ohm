noinst_PROGRAMS = dres-test

dres_test_SOURCES = dres-test.c
dres_test_CFLAGS  = @LIBFACT_CFLAGS@      \
                    @GLIB_CFLAGS@

dres_test_LDADD   = ../src/libdres.la     \
                    @LIBFACT_LIBS@        \
                    @GLIB_LIBS@ @LEXLIB@

INCLUDES = -I$(top_builddir)/include
