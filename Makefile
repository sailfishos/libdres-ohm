LEX  := flex 
YACC := bison
CC   := gcc


GLIBFLAGS := $(shell pkg-config --cflags glib-2.0) \
             $(shell pkg-config --cflags gobject-2.0)
GLIBLIBS  := $(shell pkg-config --libs glib-2.0) \
             $(shell pkg-config --libs gobject-2.0)

PROLOG_CFLAGS := $(shell pkg-config --cflags libprolog; \
                         pkg-config --cflags librelation \
                         pkg-config --cflags libfactmap)
PROLOG_LIBS   := $(shell pkg-config --libs libprolog; \
                         pkg-config --libs librelation \
                         pkg-config --libs libfactmap)

CFLAGS  := -Wall -O0 -g3 -D__DEBUG__ $(PROLOG_CFLAGS) $(GLIBFLAGS)
LDFLAGS :=  -L. -ldres -lfl -lfact $(GLIBLIBS) $(PROLOG_LIBS)

LIBDRES     := libdres.a
DRESSOURCES := lexer.c parser.c dres.c variables.c action.c

TARGETS := $(LIBDRES) dres-test #lexer-test parser-test

all: $(TARGETS)

$(LIBDRES):$(LIBDRES)($(DRESSOURCES:.c=.o))

dres-test: dres-test.c $(LIBDRES)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS) lexer.c parser.[hc] *~ *.o core.* core doc/*~

# misc. tests 
parser-test: $(SOURCES)
	$(CC) $(CFLAGS) -D__TEST_PARSER__ -o $@ $^ -lfl  -lfact $(GLIBLIBS)

lexer-test: lexer.c
	$(CC) $(CFLAGS) -D__TEST_LEXER__ -o $@ $< -lfl  -lfact $(GLIBLIBS)

# 
%.c: %.y
	$(YACC) -d -o $@ $<



# hand-crafted dependencies
lexer.c: lexer.l parser.h Makefile

parser.h: parser.c Makefile

testlexer.c: testlexer.l Makefile

testparser.c: testparser.y




