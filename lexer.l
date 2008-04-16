%{

#include <string.h>
#include "dres.h"

#ifndef __TEST_LEXER__
#  include "parser.h"
#endif

#ifdef __TEST_LEXER__
#  define DEBUG(c, fmt, args...) printf("["#c"] "fmt"\n", ## args)
#else
#  define DEBUG(c, fmt, args...)
#endif

#ifndef __TEST_LEXER__
#  define STRING(v) yylval.string = strdup(v)
#else
#  define STRING(v)
   enum {
     TOKEN_TARGET_DEF = 0,
     TOKEN_TARGET_REF,
     TOKEN_VARIABLE_REF,
     TOKEN_ACTION_NAME,
     TOKEN_ACTION_ARG,
     TOKEN_TAB,
     TOKEN_COLON,
     TOKEN_PAREN_OPEN,
     TOKEN_PAREN_CLOSE,
     TOKEN_EQUAL,
     TOKEN_LVALUE,
     TOKEN_COMMENT,
     TOKEN_EOL,
     TOKEN_NEWLINE,
     TOKEN_EOF,
     TOKEN_UNKNOWN
   };
   const char *tokenname[] = {
       "TARGET_DEF", "TARGET_REF", "VARIABLE_REF",
       "ACTION_NAME", "ACTION_ARGUMENT",
       "TAB", "COLON", "PAREN_OPEN", "PAREN_CLOSE", "EQUAL"
       "COMMENT", "EOF", "NEWLINE",
       "UNKNOWN"
   };
#endif /* __TEST_LEXER__ */

#define TOKEN(t) do {                           \
	DEBUG(lexer, "%s(\"%s\")", #t, yytext); \
        return TOKEN_##t;                       \
    } while (0)

#define IGNORE(pt) do {                          \
        DEBUG(lexer, "ignored %s(\"%s\")", #pt,  \
              *yytext == '\n' ? "\\n" : yytext); \
    } while (0)

%}


TABS            ^\t[ \t]*/[^#]+
WS              [ \t]+
OWS             [ \t]*
NEWLINE         \n
IDENT           [a-zA-Z_]+[a-zA-Z0-9_]*
VARIABLE        \${IDENT}
/* CALL seems to be broken... it causes parse errors when used in 
 * in other than <INITIAL> state
 * CALL            {IDENT}/{OWS}\(
 */
LVALUE          {VARIABLE}/{OWS}=

%s action


%%


\#              { int c;
                  DEBUG(lexer, "ignored trailing comment");
                  while ((c = input()) != EOF && c != '\n')
  		      ;                                                    }
{WS}            { IGNORE(WHITESPACE);                                      }
{NEWLINE}       { BEGIN(INITIAL); IGNORE(NEWLINE);                         }

{TABS}          { TOKEN(TAB);                                              }
{VARIABLE}      { STRING(yytext + 1); TOKEN(VARIABLE_REF);                 }

^{IDENT}/{OWS}: { STRING(yytext); TOKEN(TARGET_DEF);                       }

{IDENT}/{OWS}\( { BEGIN(action); STRING(yytext); TOKEN(ACTION_NAME);       }
{LVALUE}        { BEGIN(action); STRING(yytext+1); TOKEN(LVALUE);          }
<action>{IDENT} { STRING(yytext); TOKEN(ACTION_ARG);                       }
<action>=       { TOKEN(EQUAL);                                            }
<action>{IDENT}/{OWS}\( { STRING(yytext); TOKEN(ACTION_NAME);              }

{IDENT}         { STRING(yytext); TOKEN(TARGET_REF);                       }
:               { TOKEN(COLON);                                            }
\(              { TOKEN(PAREN_OPEN);                                       }
\)              { TOKEN(PAREN_CLOSE);                                      }
.               { STRING(yytext); TOKEN(UNKNOWN);                          }


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
  
    while ((token = yylex()) != TOKEN_EOF)
        printf("token %s: \"%s\"\n", tokenname[token],
	       token == TOKEN_EOL ? "<<EOL>>" : yytext);

    if (yyin != stdin)
        fclose(yyin);
    return 0;
}
#endif

