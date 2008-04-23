GLIBFLAGS := $(shell pkg-config --cflags glib-2.0) \
             $(shell pkg-config --cflags gobject-2.0)
GLIBLIBS  := $(shell pkg-config --libs glib-2.0) \
             $(shell pkg-config --libs gobject-2.0)

LEX    := flex 
YACC   := bison
CC     := gcc
CFLAGS := -Wall -O0 -g3 -D__DEBUG__ -I./vala -L./vala $(GLIBFLAGS)

#SOURCES := testlexer.c testparser.c dres.c
SOURCES := lexer.c parser.c dres.c variables.c

TARGETS := lexer-test parser-test dres-test

all: $(TARGETS)


dres-test: dres-test.c $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ -lfl -lfact $(GLIBLIBS) 

parser-test: $(SOURCES)
	$(CC) $(CFLAGS) -D__TEST_PARSER__ -o $@ $^ -lfl  -lfact $(GLIBLIBS)

lexer-test: lexer.c
	$(CC) $(CFLAGS) -D__TEST_LEXER__ -o $@ $< -lfl  -lfact $(GLIBLIBS)

dres.E: dres.c
	$(CC) $(CFLAGS) -E $< -o $@

%.c: %.y
	$(YACC) -d -o $@ $<

clean:
	rm -f $(TARGETS) lexer.c parser.[hc] *~ *.o core.* core doc/*~


lexer.c: lexer.l parser.h Makefile

parser.h: parser.c Makefile

testlexer.c: testlexer.l Makefile

testparser.c: testparser.y




