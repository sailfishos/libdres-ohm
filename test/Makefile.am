noinst_PROGRAMS = dres-test tx-test

dres_test_SOURCES = dres-test.c
dres_test_CFLAGS  = @LIBOHMFACT_CFLAGS@      \
                    @GLIB_CFLAGS@

dres_test_LDADD   = ../src/libdres.la     \
                    @LIBOHMFACT_LIBS@        \
                    @GLIB_LIBS@ @LEXLIB@

tx_test_SOURCES = tx-test.c
tx_test_CFLAGS  = @LIBOHMFACT_CFLAGS@      \
                  @GLIB_CFLAGS@

tx_test_LDADD   = @LIBOHMFACT_LIBS@        \
                  @GLIB_LIBS@ @LEXLIB@

INCLUDES = -I$(top_builddir)/include
