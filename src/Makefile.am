lib_LTLIBRARIES = libdres.la

libdres_la_SOURCES = parser.y lexer.l \
                     action.c builtin.c target.c \
                     factvar.c dresvar.c variables.c \
                     prereq.c graph.c dres.c \
                     vm-stack.c vm-instr.c vm-global.c vm-local.c \
                     vm-method.c vm-debug.c vm.c \
                     compiler.c

libdres_la_CFLAGS  = @GLIB_CFLAGS@ @CCOPT_VISIBILITY_HIDDEN@
libdres_la_LIBADD  = @GLIB_LIBS@ @LEXLIB@ @LIBTRACE_LIBS@


noinst_PROGRAMS = vm-test lexer-test

vm_test_SOURCES  = vm-stack.c vm-instr.c vm-global.c vm-local.c \
                   vm-method.c vm-test.c
vm_test_CFLAGS   = @LIBOHMFACT_CFLAGS@ @GLIB_CFLAGS@
vm_test_LDADD    = @LIBOHMFACT_LIBS@ @GLIB_LIBS@

lexer_test_SOURCES = lexer.l
lexer_test_CFLAGS  = -D__TEST_LEXER__=1
lexer_test_LDADD   = -lfl

#parser_test_SOURCES = lexer.l parser.y
#parser_test_CFLAGS  = -D__TEST_PARSER__=1
#parser_test_LDADD   = -lfl

INCLUDES = -I$(top_builddir)/include

MAINTAINERCLEANFILES = Makefile.in

clean-local:
	rm -f *~

