%{

#include <string.h>
#include <dres/dres.h>

#ifndef __TEST_LEXER__
#  include "parser.h"
#endif

#undef DEBUG

#if !defined(DEBUG)
#  if defined(__TEST_LEXER__)
#    define DEBUG(fmt, args...) printf("[lexer] "fmt"\n", ## args)
#  else
#    define DEBUG(fmt, args...)
#  endif
#endif

/*
 * Notes:
 *
 *   To avoid unnecessary string duplication/freeing we collect tokens to a
 *   ring buffer. Doing so allows us to have a sane lifecycle management
 *   (owner allocates/frees semantics) without having to worry about freeing
 *   unneeded tokens in the parser.
 *
 *   However the ringbuffer must be big enough to hold all the outstanding
 *   tokens collected between (bison) rule reductions by the parser. There is
 *   no ringbuffer overflow check at the moment. Instead the ringbuffer is set
 *   to be way larger than necessary for any sanely manageable ruleset.
 */

#define RINGBUFF_SIZE (16 * 1024)

static char ringbuff[RINGBUFF_SIZE];
static int  ringoffs = 0;

#ifndef __TEST_LEXER__
#  define STRING(v) do {                                \
       int __size = strlen(v) + 1;                      \
       if (ringoffs + __size + 1 >= RINGBUFF_SIZE)      \
           ringoffs = 0;                                \
       yylval.string = strcpy(ringbuff + ringoffs, v);  \
       ringoffs += __size;                              \
   } while (0)
#  define STRINGN(v, n) do {                               \
       int __size, __len;                                  \
       if ((n) > (__len = strlen(v)))                      \
           __size = __len + 1;                             \
       else                                                \
           __size = (n) + 1;                               \
       if (ringoffs + __size + 1 >= RINGBUFF_SIZE)         \
           ringoffs = 0;                                   \
       yylval.string = strncpy(ringbuff + ringoffs, v, n); \
       ringbuff[ringoffs + (n)] = '\0';                    \
       ringoffs += __size;                                 \
   } while (0)
#else
#  define STRING(v)
   enum {
     TOKEN_EOL = 0,
     TOKEN_PREFIX,
     TOKEN_INITIALIZER,
     TOKEN_SELECTOR,
     TOKEN_IDENT,
     TOKEN_DOT,
     TOKEN_NUMBER,
     TOKEN_FACTVAR,
     TOKEN_DRESVAR,
     TOKEN_COLON,
     TOKEN_PAREN_OPEN,
     TOKEN_PAREN_CLOSE,
     TOKEN_COMMA,
     TOKEN_EQUAL,
     TOKEN_TAB,
     TOKEN_EOF,
     TOKEN_UNKNOWN,
   };
   const char *tokenname[] = {
       "EOL",
       "PREFIX", "INITIALIZER", "SELECTOR",
       "IDENT", "DOT", "NUMBER", "FACTVAR", "DRESVAR",
       "COLON", "PAREN_OPEN", "PAREN_CLOSE", "COMMA", "EQUAL", "TAB",
       "<<EOF>>"
       "<<UNKNOWN>>"
   };
#endif /* __TEST_LEXER__ */

#define TOKEN(t) do {                              \
	DEBUG("%s(\"%s\")", #t,                    \
	 TOKEN_##t == TOKEN_EOL ? "EOL" : yytext); \
        if (TOKEN_##t == TOKEN_EOL) {              \
	  if (!pass_newline)			   \
	    DEBUG("EOL suppressed");	           \
	  else {				   \
	    pass_newline = 0;			   \
	    return TOKEN_##t;			   \
	  }					   \
	}					   \
	else {					   \
	  pass_newline = 1;			   \
	  return TOKEN_##t;			   \
	}					   \
    } while (0)

#define IGNORE(pt) do {                          \
        DEBUG("ignored %s(\"%s\")", #pt,         \
              *yytext == '\n' ? "\\n" : yytext); \
    } while (0)

int pass_newline = 0;
int lineno = 1;
int seen_prefix = 0;

%}

TAB		^\t/[ \t]*[^#]+
WS		[ \t]+
PREFIX		^PREFIX/({WS}|=)
OWS		[ \t]*
COMMENT_FULL	^{OWS}#.*$
COMMENT_TRAIL	#
EOL		\n+
IDENT		[a-zA-Z_]+[a-zA-Z0-9_]+
NUMBER          [+-]?[0-9]+
VARNAME         {IDENT}(\.{IDENT})*
FACTVAR		\${VARNAME}
DRESVAR		&{VARNAME}
ESCAPE          \\
INITIALIZER     \{[^{]+\}
SELECTOR        \[[^[]+\]

%s action


%%

{WS}		{ IGNORE(WHITESPACE); }
{COMMENT_FULL}  {   int c;
                    IGNORE(COMMENT_FULL);
                    if ((c = input()) != '\n')
		        unput(c);
                    else
                        lineno++;
                }
{COMMENT_TRAIL} { int c;
                  DEBUG("ignoring trailing comment");
		  while ((c = input())) {
		      if (c == '\n') {
		          unput(c);
			  break;
 	              }
                  }
                }
^[ \t]*$	{ int c;
   		  if ((c = input()) != '\n')
		      unput(c);
		  lineno++;
   		  if (!pass_newline)
		      DEBUG("ignoring empty line");
                  else
                      TOKEN(EOL);
		}
{ESCAPE}        { int c;
                  if ((c = input()) != '\n')
		      unput(c);
		  else {
                      DEBUG("ignoring escaped newline");
                      lineno++;
		      if ((c = input()) == '\t')
		          unput(' ');
		      else
		          unput(c);
                  }
                }
                
{IDENT}		{ STRING(yytext); TOKEN(IDENT);       }
{PREFIX}        { TOKEN(PREFIX); }
{INITIALIZER}   { STRINGN(yytext+1, yyleng-2); TOKEN(INITIALIZER); }
{SELECTOR}      { STRINGN(yytext+1, yyleng-2); TOKEN(SELECTOR);    }
{VARNAME}       { STRING(yytext); TOKEN(VARNAME);     }
{TAB}           { TOKEN(TAB);                         }
{EOL}		{ lineno += yyleng; TOKEN(EOL);       }
\.              { TOKEN(DOT);                         }
{NUMBER}        { STRING(yytext); TOKEN(NUMBER);      }
{FACTVAR}	{ STRING(yytext + 1); TOKEN(FACTVAR); }
{DRESVAR}	{ STRING(yytext + 1); TOKEN(DRESVAR); }
:               { TOKEN(COLON);                       }
\(              { TOKEN(PAREN_OPEN);                  }
\)              { TOKEN(PAREN_CLOSE);                 }
=		{ TOKEN(EQUAL);                       }
\+=             { TOKEN(APPEND);                      }
,               { TOKEN(COMMA);                       }

.               { STRING(yytext); TOKEN(UNKNOWN);     }

%%


int lexer_lineno(void) { return lineno; }

#ifdef __TEST_LEXER__

int depth = 0;

int
main(int argc, char *argv[])
{
    int token;
  
    if (argc > 1)
        yyin = fopen(argv[1], "r");
    else
        yyin = stdin;
  
    while ((token = yylex()) != TOKEN_EOF) {
      /*
      DEBUG("token %s: \"%s\"\n", tokenname[token],
      token == TOKEN_EOL ? "<<EOL>>" : yytext)*/;
    }

    if (yyin != stdin)
        fclose(yyin);
    return 0;
}
#endif

