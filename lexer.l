%{

#include <string.h>
#include "dres.h"

#ifndef __TEST_LEXER__
#  include "parser.h"
#endif

#if defined(__TEST_LEXER__) || 0
#  define DEBUG(c, fmt, args...) printf("["#c"] "fmt"\n", ## args)
#else
#  define DEBUG(c, fmt, args...)
#endif

#ifndef __TEST_LEXER__
#  define STRING(v) yylval.string = strdup(v)
#else
#  define STRING(v)
   enum {
     TOKEN_EOL = 0,
     TOKEN_IDENT,
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
       "IDENT", "FACTVAR", "DRESVAR",
       "COLON", "PAREN_OPEN", "PAREN_CLOSE", "COMMA", "EQUAL", "TAB",
       "<<EOF>>"
       "<<UNKNOWN>>"
   };
#endif /* __TEST_LEXER__ */

#define TOKEN(t) do {                              \
	DEBUG(lexer, "%s(\"%s\")", #t,             \
	 TOKEN_##t == TOKEN_EOL ? "EOL" : yytext); \
        if (TOKEN_##t == TOKEN_EOL) {              \
	  if (!pass_newline)			   \
	    DEBUG(lexer, "EOL suppressed");	   \
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
        DEBUG(lexer, "ignored %s(\"%s\")", #pt,  \
              *yytext == '\n' ? "\\n" : yytext); \
    } while (0)

int pass_newline;

%}

TAB		^\t/[ \t]*[^#]+
WS		[ \t]+
OWS		[ \t]*
COMMENT_FULL	^{OWS}#.*$
COMMENT_TRAIL	#
EOL		\n+
IDENT		[a-zA-Z_]+[a-zA-Z0-9_]
/*TARGET		^[a-zA-Z_]+[a-zA-Z0-9_]*/
FACTVAR		\${IDENT}
DRESVAR		&{IDENT}

%s action


%%

{WS}		{ IGNORE(WHITESPACE);   }
{COMMENT_FULL}  { int c;
                  IGNORE(COMMENT_FULL);
                  if ((c = input()) != '\n')
		      unput(c);              }
{COMMENT_TRAIL} { int c;
                  DEBUG(lexer, "ignoring trailing comment");
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
   		  if (!pass_newline)
		      DEBUG(lexer, "ignoring empty line");
                  else {
                      TOKEN(EOL);
		  }
		}
		      
		  
{TAB}           { TOKEN(TAB);                         }
{EOL}		{ TOKEN(EOL);                         }
{IDENT}		{ STRING(yytext); TOKEN(IDENT);       }
{FACTVAR}	{ STRING(yytext + 1); TOKEN(FACTVAR); }
{DRESVAR}	{ STRING(yytext + 1); TOKEN(DRESVAR); }
:               { TOKEN(COLON);                       }
\(              { TOKEN(PAREN_OPEN);                  }
\)              { TOKEN(PAREN_CLOSE);                 }
=		{ TOKEN(EQUAL);                       }
,               { TOKEN(COMMA);                       }

.               { STRING(yytext); TOKEN(UNKNOWN);     }

%%


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
      DEBUG(lexer, "token %s: \"%s\"\n", tokenname[token],
      token == TOKEN_EOL ? "<<EOL>>" : yytext)*/;
    }

    if (yyin != stdin)
        fclose(yyin);
    return 0;
}
#endif

