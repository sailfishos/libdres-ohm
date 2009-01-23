%{

#include <string.h>

#ifndef __TEST_LEXER__
#  include <dres/dres.h>
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

static int lexer_push_include(char *path);
static int lexer_pop_include (void);


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
       int   __size, __len;                                \
       if ((n) > (__len = strlen(v)))                      \
           __size = __len + 1;                             \
       else                                                \
           __size = (n) + 1;                               \
       if (ringoffs + __size + 1 >= RINGBUFF_SIZE)         \
           ringoffs = 0;                                   \
       yylval.string = strncpy(ringbuff + ringoffs, v, n); \
       ringbuff[ringoffs + (n)] = '\0';                    \
       ringoffs += __size;                                 \
   } while(0)

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
     TOKEN_PARTIAL,
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

typedef struct lexer_file_s lexer_file_t;
struct lexer_file_s {
    char             *path;
    YY_BUFFER_STATE   yybuf;
    FILE             *fp;
    int               lineno;
    lexer_file_t     *prev;
};

lexer_file_t *current   = NULL;
lexer_file_t *processed = NULL;

%}

TAB		^\t/[ \t]*[^#]+
WS		[ \t]+
OWS		[ \t]*
PREFIX		^PREFIX/({WS}|=)
INCLUDE		^INCLUDE
COMMENT_FULL	^{OWS}#.*$
COMMENT_TRAIL	#
EOL		\n+
STRING		'[^\n']*'
INTEGER		[+-]?[0-9]+
HEXINT		[+-]?0x[0-9a-fA-F]+
DOUBLE		[+-]?[0-9]+\.[0-9]+
IDENT		[a-zA-Z_]+[a-zA-Z0-9_]+
FACTNAME	\.?{IDENT}(\.{IDENT})*
FACTVAR		\${FACTNAME}
NUMBER          [+-]?[0-9]+
DRESVAR		&{IDENT}
ESCAPE          \\

%s action
%x incl

%%

{WS}		{ IGNORE(WHITESPACE); }
{COMMENT_FULL}  {   int c;
                    IGNORE(COMMENT_FULL);
                    if ((c = input()) != '\n')
		        unput(c);
                    else
                        current->lineno++;
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
		  current->lineno++;
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
                      current->lineno++;
		      if ((c = input()) == '\t')
		          unput(' ');
		      else
		          unput(c);
                  }
                }

{INCLUDE}       { BEGIN(incl); }

<incl>[ \t]*    { /* ignore whitespace before path */ }
<incl>[^ \t\n]+ {
                  if (lexer_push_include(yytext) != 0) {
                      printf("failed to open file \"%s\"\n", yytext);
                      exit(1);
                  }
                  BEGIN(INITIAL);
                }

<<EOF>>         { if (lexer_pop_include() == ENOENT)
                      yyterminate();
                }

{IDENT}		{ SAVE(yytext); TOKEN(IDENT);       }
{PREFIX}        { TOKEN(PREFIX);                    }
{TAB}           { TOKEN(TAB);                            }
{EOL}		{ current->lineno += yyleng; TOKEN(EOL); }
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
\|=             { TOKEN(PARTIAL);     }

.               { SAVE(yytext); TOKEN(UNKNOWN);     }

%%


static int
lexer_push_include(char *path)
{
    lexer_file_t *file;
    FILE         *fp;

    if ((fp = fopen(path, "r")) == NULL)
        return errno;
    else {
        if ((file = malloc(sizeof(*file))) == NULL) {
            fclose(fp);
            return ENOMEM;
        }
        memset(file, 0, sizeof(*file));
        file->path   = strdup(path);
        file->fp     = fp;
        file->lineno = 1;
        file->prev   = current;
        file->yybuf  = yy_create_buffer(fp, YY_BUF_SIZE);

        current = file;
        yypush_buffer_state(file->yybuf);

        DEBUG("PUSH \"%s\"...\n", file->path);
    }

    return 0;
}


static int
lexer_pop_include(void)
{
    lexer_file_t *old;

    if (current == NULL)
        return ENOENT;

    old     = current;
    current = old->prev;

    old->prev = processed;
    processed = old;

    DEBUG("POP: popped \"%s\"\n", old->path);
    fclose(old->fp);
    free(old->path);

    if (current != NULL) {
        yypop_buffer_state();
        DEBUG("POP: current in \"%s\"\n", current->path);
        return 0;
    }
    else
        return ENOENT;
}


int
lexer_open(char *path)
{
    return lexer_push_include(path);
}


int
lexer_lineno(void)
{
    return current ? current->lineno : 1;
}




#ifdef __TEST_LEXER__

int depth = 0;

int
main(int argc, char *argv[])
{
    int token;
  
    if (argc != 2) {
        fprintf(stderr, "usage: %s input-file\n", argv[0]);
        exit(1);
    }

    lexer_open(argv[1]);
  
    while ((token = yylex()) != TOKEN_EOF) {
      /*
      DEBUG("token %s: \"%s\"\n", tokenname[token],
      token == TOKEN_EOL ? "<<EOL>>" : yytext)*/;
    }

    return 0;

    (void)ringbuff;
    (void)ringoffs;
}
#endif

