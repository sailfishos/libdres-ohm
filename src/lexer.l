%{

#include <string.h>

#ifndef __TEST_LEXER__
#  include <dres/dres.h>
#  include "parser-types.h"
#  include "parser.h"
#endif

#undef DEBUG

#if !defined(DEBUG)
#  if defined(__TEST_LEXER__) || 1
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
#  define SAVE(v) do {                                  \
       int __size = strlen(v) + 1;                      \
       if (ringoffs + __size + 1 >= RINGBUFF_SIZE)      \
           ringoffs = 0;                                \
       yylval.string = strcpy(ringbuff + ringoffs, v);  \
       ringoffs += __size;                              \
   } while (0)

#  define SAVEN(v, n) do {                                 \
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

#  define INTEGER(v, base) do {                            \
       yylval.integer = strtol(v, NULL, base);             \
   } while (0)

#  define DOUBLE(v) do {                                   \
       yylval.dbl = strtod(v, NULL);                       \
   } while (0)

#else
#  define SAVE(v)
#  define SAVEN(v, n)
#  define INTEGER(v, base)
#  define DOUBLE(v)
   enum {
     TOKEN_EOL = 0,
     TOKEN_PREFIX,
     TOKEN_IDENT,
     TOKEN_FACTNAME,
     TOKEN_DOT,
     TOKEN_STRING,
     TOKEN_INTEGER,
     TOKEN_DOUBLE,
     TOKEN_FACTVAR,
     TOKEN_DRESVAR,
     TOKEN_COLON,
     TOKEN_PAREN_OPEN,
     TOKEN_PAREN_CLOSE,
     TOKEN_CURLY_OPEN,
     TOKEN_CURLY_CLOSE,
     TOKEN_BRACE_OPEN,
     TOKEN_BRACE_CLOSE,
     TOKEN_COMMA,
     TOKEN_EQUAL,
     TOKEN_APPEND,
     TOKEN_TAB,
     TOKEN_EOF,
     TOKEN_UNKNOWN,
   };
   const char *tokenname[] = {
       "EOL",
       "PREFIX",
       "IDENT", "FACTNAME", "DOT", 
       "STRING", "INTEGER", "DOUBLE", "FACTVAR", "DRESVAR",
       "COLON",
       "PAREN_OPEN", "PAREN_CLOSE",
       "CURLY_OPEN", "CURLY_CLOSE",
       "BREACE_OPEN", "BRACE_CLOSE",
       "COMMA", "EQUAL", "APPEND", "TAB",
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
OWS		[ \t]*
PREFIX		^PREFIX/({WS}|=)
COMMENT_FULL	^{OWS}#.*$
COMMENT_TRAIL	#
EOL		\n+
STRING		'[^\n']*'
INTEGER		[+-]?[0-9]+
HEXINT		[+-]?0x[0-9a-fA-F]+
DOUBLE		[+-]?[0-9]+\.[0-9]+
IDENT		[a-zA-Z_]+[a-zA-Z0-9_]+
FACTNAME	{IDENT}(\.{IDENT})*
FACTVAR		\${FACTNAME}
NUMBER          [+-]?[0-9]+
DRESVAR		&{IDENT}
ESCAPE          \\

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

{IDENT}		{ SAVE(yytext); TOKEN(IDENT);       }
{PREFIX}        { TOKEN(PREFIX);                    }
{TAB}           { TOKEN(TAB);                         }
{EOL}		{ lineno += yyleng; TOKEN(EOL);       }
\.              { TOKEN(DOT);                         }

{STRING}	{ SAVEN(yytext+1, yyleng-2); TOKEN(STRING);   }
{INTEGER}	{ INTEGER(yytext, 10);       TOKEN(INTEGER);  }
{DOUBLE}	{ DOUBLE(yytext);            TOKEN(DOUBLE);   }
{FACTNAME}	{ SAVE(yytext);              TOKEN(FACTNAME); }
{FACTVAR}	{ SAVE(yytext + 1);          TOKEN(FACTVAR);  }
{DRESVAR}       { SAVE(yytext + 1);          TOKEN(DRESVAR);  }

\{		{ TOKEN(CURLY_OPEN);  }
\}		{ TOKEN(CURLY_CLOSE); }
\[		{ TOKEN(BRACE_OPEN);  }
\]		{ TOKEN(BRACE_CLOSE); }
\(              { TOKEN(PAREN_OPEN);  }
\)              { TOKEN(PAREN_CLOSE); }
:               { TOKEN(COLON);       }
,               { TOKEN(COMMA);       }
=		{ TOKEN(EQUAL);       }
\+=             { TOKEN(APPEND);      }

.               { SAVE(yytext); TOKEN(UNKNOWN);     }

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

