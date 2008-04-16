LEX    := flex 
CC     := gcc
CFLAGS := -Wall -O0 -g3 -D__DEBUG__

SOURCES := lexer.c parser.c dres.c

TARGETS := lexer-test parser-test dres-test

all: $(TARGETS)


dres-test: dres-test.c $(SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ -lfl

parser-test: $(SOURCES)
	$(CC) $(CFLAGS) -D__TEST_PARSER__ -o $@ $^ -lfl

lexer-test: lexer.c
	$(CC) $(CFLAGS) -D__TEST_LEXER__ -o $@ $< -lfl

%.c: %.y
	$(YACC) -d -o $@ $<

clean:
	rm -f $(TARGETS) lexer.c parser.[hc] *~ *.o core.* core doc/*~


lexer.c: lexer.l parser.h Makefile

parser.h: parser.c Makefile






