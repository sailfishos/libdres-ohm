%{ /* -*- c -*- */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>

#include <dres/dres.h>

#include "parser-types.h"
#include "parser.h"




#ifndef FALSE
#  define FALSE 0
#  define TRUE (!FALSE)
#endif


#define DEBUG(format, args...) do {            \
        if (lexer.debug)                       \
            printf("D: "format"\n" , ## args); \
    } while (0)
    

#define IGNORE(type) do {                                               \
        DEBUG("%s:%d: ignored %s", lexer_file(), lexer_line(), #type);  \
    } while (0)



/*****************************************************************************
 *                *** lexical analyser type definitions ***                  *
 *****************************************************************************/

/*
 * To avoid continuous string duplication and freeing in the parser we
 * instead collect tokens to a ring buffer. Doing so allows us to have
 * reasonably sane lifecycle management with "owner allocates/releases"
 * semantics without having to worry about freeing unneeded temporary
 * tokens in the parser.
 *
 * The only gotcha is that the ring buffer must be big enough to hold
 * all the values of the outstanding tokens between bison rule reductions
 * by the parser. Currently there is no ring buffer overflow check in
 * this code. Instead the ring buffer is now simply set to be way larger
 * than necessary for any sanely manageable ruleset.
 */

#define RINGBUF_SIZE (16 * 1024)                /* token ring buffer size */

typedef struct lexer_file_s lexer_file_t;

struct lexer_file_s {
    YY_BUFFER_STATE   yybuf;                    /* flex input buffer */
    FILE             *fp;                       /* input stream */
    char             *path;                     /* input file path */
    int               line;                     /* input line number */
    lexer_file_t     *prev;                     /* previous input */
};

typedef struct {
    char          tokens[RINGBUF_SIZE];         /* token value ring buffer */
    int           offset;                       /* ring buffer offset */
    int           debug;                        /* debug lexical analysis ? */
    int           pass_newline;                 /* pass next newline thru ? */
    lexer_file_t *current;                      /* input file being processed */
    lexer_file_t *processed;                    /* processed include */
} lexer_t;


static lexer_t lexer = { .debug = TRUE };


static int lexer_push_include(char *path);
static int lexer_pop_include (void);

static int yywrap(void);

int   lexer_line(void);
char *lexer_file(void);

static inline int parse_integer(lexer_t *l, char *str, int *value);
static inline int parse_double(lexer_t *l, char *str, double *value);



/*****************************************************************************
 *                    *** token ring buffer management ***                   *
 *****************************************************************************/

char *
lexer_printable_token(const char *token)
{
    static char  buf[PATH_MAX];
    char        *q;
    const char  *p;
    int          n;

    p = token;
    q = buf;
    n = sizeof(buf) - 1;
    while (*p && n > 0) {
        switch (*p) {
        case '\n':
        case '\r':
            if (n > 2) {
                *q++ = '\\';
                *q++ = *p == '\n' ? 'n': 'r';
                n -= 2;
            }
            else {
                *q++ = '.';
                n--;
            }
            break;
        case '\t':
            if (n > 2) {
                *q++ = '\\'; *q++ = 't';
                n -= 2;
            }
            else {
                *q++ = '.';
                n--;
            }
            break;
        default:
            *q++ = *p;
            n--;
        }
        p++;
    }
    *q = '\0';
    
    return buf;
}


static char *
token_saven(lexer_t *l, char *token, int length)
{
    int   size;
    char *saved;

    size = strlen(token);
    
    if (length > size)
        length = size;

    size = length + 1;

    if (l->offset + size >= RINGBUF_SIZE)
        l->offset = 0;
    
    saved = l->tokens + l->offset;
    l->offset += size;

    strncpy(saved, token, length);
    saved[length] = '\0';

    yylval.any.token  = saved;
    yylval.any.lineno = l->current->line;
    DEBUG("saved token '%s'", lexer_printable_token(saved));

    return saved;
}

static char *
token_save(lexer_t *l, char *token)
{
    return token_saven(l, token, strlen(token));
}



/*****************************************************************************
 *                      *** token parsing & passing ***                      *
 *****************************************************************************/


static void
lexer_error(lexer_t *l, const char *format, ...)
{
    va_list ap;
    
    fprintf(stderr, "dres: lexical error on line %d in file %s",
            l->current->line, l->current->path);
    
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
}


#define PASS_TOKEN(type) do {                                           \
        if (TOKEN_##type == TOKEN_EOL) {                                \
            lexer_error(&lexer, "internal error: TOKEN(EOL) !");        \
            return TOKEN_LEXER_ERROR;                                   \
        }                                                               \
                                                                        \
        DEBUG("%s:%d: %s ('%s')", lexer_file(), lexer_line(),           \
              #type, yytext);                                           \
                                                                        \
        token_save(&lexer, yytext);                                     \
                                                                        \
        lexer.pass_newline = TRUE;                                      \
        return TOKEN_##type;                                            \
    } while (0)


#define PASS_NUMBER(type) do {                                          \
        char *__token;                                                  \
        int   __ok;                                                     \
                                                                        \
        DEBUG("%s:%d: %s ('%s')", lexer_file(), lexer_line(),           \
              #type, yytext);                                           \
                                                                        \
        __token = token_save(&lexer, yytext);                           \
                                                                        \
        switch (TOKEN_##type) {                                         \
        case TOKEN_INTEGER:                                             \
            __ok = parse_integer(&lexer,__token,&yylval.integer.value); \
            break;                                                      \
        case TOKEN_DOUBLE:                                              \
            __ok = parse_double(&lexer, __token, &yylval.dbl.value);    \
            break;                                                      \
        default:                                                        \
            lexer_error(&lexer, "invalid numeric type %s", #type);      \
            __ok = FALSE;                                               \
        }                                                               \
                                                                        \
        lexer.pass_newline = TRUE;                                      \
                                                                        \
        return __ok ? TOKEN_##type : TOKEN_LEXER_ERROR;                 \
    } while (0)


#define PASS_STRING(type) do {                                          \
        char *__token, *__value;                                        \
        int   __len;                                                    \
                                                                        \
        DEBUG("%s:%d: %s ('%s')", lexer_file(), lexer_line(),           \
              #type, yytext);                                           \
                                                                        \
        __len   = yyleng;                                               \
        __value = yytext;                                               \
                                                                        \
        if (TOKEN_##type == TOKEN_STRING) {                             \
            if (__value[0] == '"' || __value[0] == '\'')                \
                __token = token_saven(&lexer, __value + 1, __len - 2);  \
            else                                                        \
                __token = token_save(&lexer, __value);                  \
        }                                                               \
        else                                                            \
            __token = token_save(&lexer, __value);                      \
                                                                        \
        yylval.string.value = __token;                                  \
                                                                        \
        lexer.pass_newline  = TRUE;                                     \
        return TOKEN_##type;                                            \
    } while (0)


#define PASS_IDENT(type) do {                                           \
        char *__token, *__value;                                        \
                                                                        \
        __value = yytext;                                               \
                                                                        \
        DEBUG("%s:%d: %s ('%s')", lexer_file(), lexer_line(),           \
              #type, __value);                                          \
                                                                        \
        if (TOKEN_##type == TOKEN_FACTVAR ||                            \
            TOKEN_##type == TOKEN_DRESVAR)                              \
            __value++;                                                  \
                                                                        \
        __token = token_save(&lexer, __value);                          \
                                                                        \
        yylval.string.value = __token;                                  \
                                                                        \
        lexer.pass_newline  = TRUE;                                     \
        return TOKEN_##type;                                            \
    } while (0)


#define PROCESS_EOL(kind) do {                                          \
        lexer.current->line++;                                          \
        if (!lexer.pass_newline) {                                      \
            DEBUG("%s:%d: ignore EOL (%s)", lexer_file(), lexer_line(), \
                  #kind);                                               \
            IGNORE(EOL);                                                \
        }                                                               \
        else {                                                          \
            DEBUG("%s:%d: EOL (%s)", lexer_file(), lexer_line(),#kind); \
                                                                        \
            token_saven(&lexer, "\n", 1);                               \
                                                                        \
            lexer.pass_newline = FALSE;                                 \
            return TOKEN_EOL;                                           \
        }                                                               \
    } while (0)


#define PUSH_BACK_EOL() do {                                            \
        lexer.current->line--;                                          \
        unput('\n');                                                    \
    } while (0)


#define PROCESS_ESCAPE() do {                                           \
        int __c;                                                        \
                                                                        \
        switch ((__c = input())) {                                      \
        case '\n':                                                      \
            DEBUG("%s:%d: ignore escaped '\\n'",                        \
                  lexer_file(), lexer_line());                          \
            lexer.current->line++;                                      \
            /* kludge to get tabulation after escaped newline work */   \
            if ((__c = input()) == '\t')                                \
                unput(' ');                                             \
            else                                                        \
                unput(__c);                                             \
            break;                                                      \
        default:                                                        \
            DEBUG("%s:%d: escaped '%c'",                                \
                  lexer_file(), lexer_line(), __c);                     \
            unput(__c);                                                 \
        }                                                               \
    } while (0)



static inline int
parse_integer(lexer_t *l, char *str, int *value)
{
    char *end;

    *value = (int)strtoll(str, &end, 10);
    
    if (!*end)
        return TRUE;
    else {
        lexer_error(l, "invalid integer \"%s\"", str);
        return FALSE;
    }
}


static inline int
parse_double(lexer_t *l, char *str, double *value)
{
    char *end;

    *value = strtod(str, &end);
    
    if (!*end)
        return TRUE;
    else {
        lexer_error(l, "invalid double precision floating \"%s\"", str);
        return FALSE;
    }
}


%}

TAB		^\t/[ \t]*[^#]+
WHITESPACE	[ \t]+
EMPTY_LINE      [ \t]*$
PREFIX		^PREFIX/({WHITESPACE}|=)
INCLUDE		^INCLUDE
COMMENT_FULL    ^[ \t]*#.*$
COMMENT_TRAIL    [ \t]*#.*$
VARIABLES       ^variables
EOL		\n
STRING		('[^\n']*')|(\"[^\n\"]*\")
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

{WHITESPACE}          { IGNORE(WHITESPACE);    }
{COMMENT_FULL}        { IGNORE(COMMENT_FULL);
                        PUSH_BACK_EOL();       }
{COMMENT_TRAIL}       { IGNORE(COMMENT_TRAIL);
                        PUSH_BACK_EOL();         }
{ESCAPE}              { PROCESS_ESCAPE();      }

{INCLUDE}             { BEGIN(incl);           }

<incl>{WHITESPACE}    { IGNORE(WHITESPACE);    }
<incl>[^ \t\n]+       {
                        if (lexer_push_include(yytext) != 0) {
                            fprintf(stderr, "failed to open file \"%s\"\n",
                                    yytext);
                            exit(1);
                        }
                        else
                            BEGIN(INITIAL);
                      }

<<EOF>>               {
                        if (lexer_pop_include() == ENOENT)
                            yyterminate();
                      }

{VARIABLES}           { PASS_TOKEN(VARIABLES);   }

if                    { PASS_TOKEN(IF);          }
then                  { PASS_TOKEN(THEN);        }
else                  { PASS_TOKEN(ELSE);        }
end                   { PASS_TOKEN(END);         }

{IDENT}		      { PASS_IDENT(IDENT);       }
{PREFIX}              { PASS_TOKEN(PREFIX);      }
{TAB}                 { PASS_TOKEN(TAB);         }
{EOL}		      { PROCESS_EOL(EOL);        }
\.                    { PASS_TOKEN(DOT);         }

{STRING}	      { PASS_STRING(STRING);     }
{INTEGER}	      { PASS_NUMBER(INTEGER);    }
{DOUBLE}	      { PASS_NUMBER(DOUBLE);     }
{FACTNAME}	      { PASS_IDENT(FACTNAME);    }
{FACTVAR}	      { PASS_IDENT(FACTVAR);     }
{DRESVAR}             { PASS_IDENT(DRESVAR);     }

\{		      { PASS_TOKEN(CURLY_OPEN);  }
\}		      { PASS_TOKEN(CURLY_CLOSE); }
\[		      { PASS_TOKEN(BRACE_OPEN);  }
\]		      { PASS_TOKEN(BRACE_CLOSE); }
\(                    { PASS_TOKEN(PAREN_OPEN);  }
\)                    { PASS_TOKEN(PAREN_CLOSE); }
:                     { PASS_TOKEN(COLON);       }
,                     { PASS_TOKEN(COMMA);       }

==                    { PASS_TOKEN(EQ);          }
!=                    { PASS_TOKEN(NE);          }
\<=                   { PASS_TOKEN(LE);          }
\<                    { PASS_TOKEN(LT);          }
>=                    { PASS_TOKEN(GE);          }
>                     { PASS_TOKEN(GT);          }
!                     { PASS_TOKEN(NOT);         }
\|\|		      { PASS_TOKEN(OR);          }
\&\&		      { PASS_TOKEN(AND);         }


=		      { PASS_TOKEN(EQUAL);       }
\+=                   { PASS_TOKEN(APPEND);      }
\|=                   { PASS_TOKEN(PARTIAL);     }
\*=                   { PASS_TOKEN(REPLACE);     }

.                     { PASS_TOKEN(UNKNOWN);     }

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
        file->line = 1;
        file->prev   = lexer.current;
        file->yybuf  = yy_create_buffer(fp, YY_BUF_SIZE);

        lexer.current = file;
        yypush_buffer_state(file->yybuf);

        DEBUG("PUSH \"%s\"...", file->path);
    }

    return 0;
}

static int
lexer_pop_include(void)
{
    lexer_file_t *old;

    if (lexer.current == NULL)
        return ENOENT;

    old = lexer.current;
    lexer.current = old->prev;

    old->prev = lexer.processed;
    lexer.processed = old;

    DEBUG("POP: popped \"%s\"", old->path);
    fclose(old->fp);
    free(old->path);

    if (lexer.current != NULL) {
        yypop_buffer_state();
        DEBUG("POP: current in \"%s\"", lexer.current->path);
        return 0;
    }
    else
        return ENOENT;
}


int
lexer_open(char *path)
{
    char *var;

    if ((var = getenv("DRES_LEXER_DEBUG")) != NULL &&
        (!strcasecmp(var, "yes") || !strcasecmp(var, "true")))
        lexer.debug = TRUE;
    else
        lexer.debug = FALSE;

    return lexer_push_include(path);
}


char *
lexer_file(void)
{
    return lexer.current ? lexer.current->path : "<unknown>";
}


int
lexer_line(void)
{
    return lexer.current ? lexer.current->line : 1;
}


static int yywrap(void)
{
    return 1;
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
