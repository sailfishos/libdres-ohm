lib_LTLIBRARIES = libdres.la

libdres_la_SOURCES = parser.y lexer.l \
                     action.c builtin.c target.c literal.c \
                     factvar.c dresvar.c variables.c scope.c \
                     prereq.c graph.c dres.c
libdres_la_CFLAGS  = @GLIB_CFLAGS@ @CCOPT_VISIBILITY_HIDDEN@
libdres_la_LIBADD  = @GLIB_LIBS@ @LIBTRACE_LIBS@


noinst_PROGRAMS = vm-test

vm_test_SOURCES  = vm-stack.c vm-instr.c vm-global.c vm-method.c vm-test.c
vm_test_CFLAGS   = @LIBFACT_CFLAGS@ @GLIB_CFLAGS@ -DVM_HAS_GLOBALS=1
vm_test_LDADD    = @LIBFACT_LIBS@ @GLIB_LIBS@

INCLUDES = -I$(top_builddir)/include

MAINTAINERCLEANFILES = Makefile.in

clean-local:
	rm -f *~

