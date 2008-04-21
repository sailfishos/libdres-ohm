%{

#include <string.h>
#include "dres.h"

#ifndef __TEST_LEXER__
#  include "parser.h"
#endif

#if defined(__TEST_LEXER__) || 1
#  define DEBUG(fmt, args...) printf("[lexer] "fmt"\n", ## args)
#else
#  define DEBUG(fmt, args...)
#endif

#ifndef __TEST_LEXER__
#  define STRING(v) yylval.string = strdup(v)
#else
#  define STRING(v)
   enum {
     TOKEN_EOL = 0,
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

int pass_newline;
int lineno = 1;

%}

TAB		^\t/[ \t]*[^#]+
WS		[ \t]+
OWS		[ \t]*
COMMENT_FULL	^{OWS}#.*$
COMMENT_TRAIL	#
EOL		\n+
IDENT		[a-zA-Z_]+[a-zA-Z0-9_]
NUMBER          [+-]?[0-9]+
/*TARGET		^[a-zA-Z_]+[a-zA-Z0-9_]*/
FACTVAR		\${IDENT}
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
                  }
                }
                 
		  
{TAB}           { TOKEN(TAB);                         }
{EOL}		{ lineno += yyleng; TOKEN(EOL);       }
{IDENT}		{ STRING(yytext); TOKEN(IDENT);       }
\.              { TOKEN(DOT);                         }
{NUMBER}        { STRING(yytext); TOKEN(NUMBER);      }
{FACTVAR}	{ STRING(yytext + 1); TOKEN(FACTVAR); }
{DRESVAR}	{ STRING(yytext + 1); TOKEN(DRESVAR); }
:               { TOKEN(COLON);                       }
\(              { TOKEN(PAREN_OPEN);                  }
\)              { TOKEN(PAREN_CLOSE);                 }
=		{ TOKEN(EQUAL);                       }
,               { TOKEN(COMMA);                       }

.               { STRING(yytext); TOKEN(UNKNOWN);     }

%%


int lexer_lineno(void) { return lineno; }

#ifdef __TEST_LEXER__
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

