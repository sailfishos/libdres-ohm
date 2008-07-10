%{

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <dres/dres.h>
#include <glib-object.h>
#include "parser-types.h"

#if !defined(DEBUG)
#  if defined(__TEST_PARSER__) || 0
#    define DEBUG(fmt, args...) printf("[parser] "fmt"\n", ## args)
#  else
#    define DEBUG(fmt, args...)
#  endif
#endif

#define FQFN(name) (factname(name))

/* parser/lexer interface */
int  yylex  (void);
void yyerror(dres_t *dres, char const *);

extern FILE *yyin;

/* fact prefix support */
int   set_prefix(char *);
char *factname  (char *);

static char *current_prefix;



%}



%union {
    int             integer;
    double          dbl;
    char           *string;
    dres_target_t  *target;
    dres_prereq_t  *prereq;
    dres_varref_t   varref;
    dres_arg_t     *arg;
    dres_local_t   *local;
    dres_action_t   variables;
    dres_field_t    field;
    dres_select_t  *select;
    dres_call_t    *call;
    dres_action_t  *action;

    dres_init_t        *init;
    dres_initializer_t *initializer;
}

%defines
%parse-param {dres_t *dres}

%token <string>  TOKEN_PREFIX
%token <string>  TOKEN_IDENT
%token <string>  TOKEN_FACTNAME
%token           TOKEN_DOT "."
%token <string>  TOKEN_STRING
%token <integer> TOKEN_INTEGER
%token <dbl>     TOKEN_DOUBLE
%token <string>  TOKEN_FACTVAR
%token <string>  TOKEN_DRESVAR
%token           TOKEN_COLON ":"
%token           TOKEN_PAREN_OPEN  "("
%token           TOKEN_PAREN_CLOSE ")"
%token           TOKEN_CURLY_OPEN  "{"
%token           TOKEN_CURLY_CLOSE "}"
%token           TOKEN_BRACE_OPEN  "["
%token           TOKEN_BRACE_CLOSE "]"
%token           TOKEN_COMMA ","
%token           TOKEN_EQUAL   "="
%token           TOKEN_APPEND "+="
%token           TOKEN_TAB "\t"
%token           TOKEN_EOL
%token           TOKEN_EOF
%token           TOKEN_UNKNOWN

%type <target>  rule
%type <integer> prereq
%type <prereq>  prereqs
%type <prereq>  optional_prereqs
%type <varref>  varref
%type <field>     field
%type <init>      ifields
%type <select>    sfields
%type <initializer> initializer
%type <initializer> initializers
%type <action>      optional_actions
%type <action>      actions
%type <action>      action
%type <call>        call
%type <arg>         arg
%type <arg>         args
%type <local>       local
%type <local>       locals

%%


input: optional_prefix optional_initializers rules

optional_prefix: /* empty */
        |        prefix
	;

prefix: TOKEN_PREFIX "=" TOKEN_FACTNAME TOKEN_EOL {
            set_prefix($3);
#if 0
	    printf("*** prefix set to \"%s\"\n", current_prefix);
#endif
        }

optional_initializers: { dres->initializers = NULL; }
        | initializers { dres->initializers = $1; dres_dump_init(dres); }
        ;

initializers: initializer          { $$ = $1; }
        | initializers initializer {
            dres_initializer_t *init;
	    if ($1 != NULL) {
                for (init = $1; init->next; init = init->next)
                    ;
                init->next = $2;
            }
            $$ = $1;
        }
        ;

initializer: TOKEN_FACTVAR assign_op "{" ifields "}" TOKEN_EOL {
            dres_initializer_t *init;
            
            if ((init = ALLOC(dres_initializer_t)) == NULL)
	        YYABORT;
            init->variable = dres_factvar_id(dres, FQFN($1));
            init->fields   = $4;
            init->next     = NULL;

            $$ = init;
        }
        | prefix { $$ = NULL; }
        ;

assign_op: "=" | "+=";

ifields: field {
            if (($$ = ALLOC(dres_init_t)) == NULL)
	        YYABORT;
            $$->field = $1;
            $$->next  = NULL;
        }
        | ifields "," field {
            dres_init_t *f, *p;
            if ((f = ALLOC(dres_init_t)) == NULL)
                YYABORT;
            for (p = $1; p->next; p = p->next)
                ;
	    p->next = f;
            f->field = $3;
            f->next  = NULL;
            $$ = $1;
        }
        ;

sfields: field {
            if (($$ = ALLOC(dres_select_t)) == NULL)
	        YYABORT;
            $$->field = $1;
            $$->next  = NULL;
        }
        | sfields "," field {
            dres_select_t *f, *p;
            if ((f = ALLOC(dres_select_t)) == NULL)
                YYABORT;
            for (p = $1; p->next; p = p->next)
                ;
	    p->next = f;
            f->field = $3;
            f->next  = NULL;
            $$ = $1;
        }
        ;


field: TOKEN_IDENT ":" TOKEN_INTEGER {
            $$.name = STRDUP($1);
            $$.value.type = DRES_TYPE_INTEGER;
	    $$.value.v.i  = $3;
        }
        | TOKEN_IDENT ":" TOKEN_DOUBLE {
            $$.name = STRDUP($1);
            $$.value.type = DRES_TYPE_DOUBLE;
	    $$.value.v.d  = $3;
        }
        | TOKEN_IDENT ":" TOKEN_STRING {
            $$.name = STRDUP($1);
            $$.value.type = DRES_TYPE_STRING;
            $$.value.v.s  = STRDUP($3);
        }
        ;

rules:    rule
	| rules rule
        | rules prefix
	;


rule: TOKEN_IDENT ":" optional_prereqs TOKEN_EOL optional_actions {
            dres_target_t *t = dres_lookup_target(dres, $1);
            t->prereqs = $3;
            t->actions = $5;
            t->id      = DRES_DEFINED(t->id);
            $$ = t;
        }
	;

optional_prereqs: /* empty */    { $$ = NULL; }
	| prereqs                { $$ = $1;   }
	;

prereqs:  prereq                 { $$ = dres_new_prereq($1);          }
	| prereqs prereq         { dres_add_prereq($1, $2); $$ = $1;  }
	;

prereq:   TOKEN_IDENT            { $$ = dres_target_id(dres, $1);    }
	| TOKEN_FACTVAR          {
            dres_variable_t *v;
            $$ = dres_factvar_id(dres, FQFN($1));
            if ((v = dres_lookup_variable(dres, $$)) != NULL)
                v->flags |= DRES_VAR_PREREQ;
        }
	;

optional_actions: /* empty */ { $$ = NULL; }
	| actions             { $$ = $1;   }
	;

actions:  action         { $$ = $1; }
	| actions action {
            dres_action_t *a;

            for (a = $1; a->next; a = a->next)
                ;

            a->next = $2;
            $$      = $1;
        }
	;

action:   TOKEN_TAB varref "=" call TOKEN_EOL {
            if (($$ = ALLOC(dres_action_t)) == NULL)
                YYABORT;

            $$->type   = DRES_ACTION_CALL;
            $$->lvalue = $2;
            $$->call   = $4;
        }
        | TOKEN_TAB call TOKEN_EOL {
            if (($$ = ALLOC(dres_action_t)) == NULL)
                YYABORT;

            $$->type            = DRES_ACTION_CALL;
            $$->lvalue.variable = DRES_ID_NONE;
	    $$->lvalue.selector = NULL;
	    $$->lvalue.field    = NULL;
            $$->call            = $2;
        }
        | TOKEN_TAB varref "=" varref TOKEN_EOL {
            if (($$ = ALLOC(dres_action_t)) == NULL)
                YYABORT;

            $$->type   = DRES_ACTION_VARREF;
            $$->lvalue = $2;
            $$->rvalue = $4;
        }
        | TOKEN_TAB varref "=" TOKEN_INTEGER {
            if (($$ = ALLOC(dres_action_t)) == NULL)
                YYABORT;

            $$->type   = DRES_ACTION_VALUE;
            $$->lvalue = $2;
            $$->value.type = DRES_TYPE_INTEGER;
            $$->value.v.i  = $4;
        }
        | TOKEN_TAB varref "=" TOKEN_DOUBLE {
            $$->type   = DRES_ACTION_VALUE;
            $$->lvalue = $2;
            $$->value.type = DRES_TYPE_DOUBLE;
            $$->value.v.d  = $4;
        }
        | TOKEN_TAB varref "=" TOKEN_STRING {
            $$->type   = DRES_ACTION_VALUE;
            $$->lvalue = $2;
            $$->value.type = DRES_TYPE_STRING;
            $$->value.v.s  = STRDUP($4);
        }
	| TOKEN_TAB varref "=" TOKEN_IDENT {
            $$->type   = DRES_ACTION_VALUE;
            $$->lvalue = $2;
            $$->value.type = DRES_TYPE_STRING;
            $$->value.v.s  = STRDUP($4);
        }
        ;

varref: TOKEN_FACTVAR {
            $$.variable = dres_factvar_id(dres, FQFN($1));
            $$.selector = NULL;
            $$.field    = NULL;
        }
        | TOKEN_FACTVAR ":" TOKEN_IDENT {
            $$.variable = dres_factvar_id(dres, FQFN($1));
            $$.selector = NULL;
            $$.field    = STRDUP($1);
        }
        | TOKEN_FACTVAR "[" sfields "]" {
            $$.variable = dres_factvar_id(dres, FQFN($1));
            $$.selector = $3;
            $$.field    = NULL;
        }
        | TOKEN_FACTVAR "[" sfields "]" ":" TOKEN_IDENT {
            $$.variable = dres_factvar_id(dres, FQFN($1));
            $$.selector = $3;
            $$.field    = STRDUP($6);
        }
        ;


call: TOKEN_IDENT "(" args "," locals ")" {
            $$ = dres_new_call($1, $3, $5);
        }
	| TOKEN_IDENT "(" args ")" {
	    $$ = dres_new_call($1, $3, NULL);
        }
	| TOKEN_IDENT "(" locals ")" {
	    $$ = dres_new_call($1, NULL, $3);
        }
	| TOKEN_IDENT "(" ")" {
            $$ = dres_new_call($1, NULL, NULL);
        }
	;

args: arg { $$ = $1; }
        | args "," arg {
            dres_arg_t *a;
            for (a = $1; a->next != NULL; a = a->next)
                ;
            a->next = $3;
            $$      = $1;
        }
        ;

arg:      TOKEN_INTEGER {
            if (($$ = ALLOC(dres_arg_t)) == NULL)
                YYABORT;
            $$->value.type = DRES_TYPE_INTEGER;
            $$->value.v.i  = $1;
            $$->next = NULL;
        }
        | TOKEN_DOUBLE {
            if (($$ = ALLOC(dres_arg_t)) == NULL)
                YYABORT;
            $$->value.type = DRES_TYPE_DOUBLE;
            $$->value.v.d  = $1;
            $$->next = NULL;
        }
        | TOKEN_STRING {
            if (($$ = ALLOC(dres_arg_t)) == NULL)
                YYABORT;
            $$->value.type = DRES_TYPE_STRING;
            $$->value.v.s  = STRDUP($1);
            $$->next = NULL;

            if ($$->value.v.s == NULL)
                YYABORT;
        }
        | TOKEN_IDENT {
            if (($$ = ALLOC(dres_arg_t)) == NULL)
                YYABORT;
            $$->value.type = DRES_TYPE_STRING;
            $$->value.v.s  = STRDUP($1);
            $$->next = NULL;

            if ($$->value.v.s == NULL)
                YYABORT;
        }
        | TOKEN_FACTVAR {
            if (($$ = ALLOC(dres_arg_t)) == NULL)
                YYABORT;
            $$->value.type = DRES_TYPE_FACTVAR;
            $$->value.v.id = dres_factvar_id(dres, FQFN($1));
            $$->next = NULL;

            if ($$->value.v.id == DRES_ID_NONE)
                YYABORT;
        }
        | TOKEN_DRESVAR {
            if (($$ = ALLOC(dres_arg_t)) == NULL)
                YYABORT;
            $$->value.type = DRES_TYPE_DRESVAR;
            $$->value.v.id = dres_dresvar_id(dres, $1);
            $$->next = NULL;

            if ($$->value.v.id == DRES_ID_NONE)
                YYABORT;
        }


locals:   local { $$ = $1; }
        | locals "," local {
            dres_local_t *l;

            for (l = $1; l->next != NULL; l = l->next)
                ;
            l->next = $3;
            $$ = $1;
        }


local:    TOKEN_DRESVAR "=" TOKEN_INTEGER {
            if (($$ = ALLOC(dres_local_t)) == NULL)
                YYABORT;
            $$->id         = dres_dresvar_id(dres, $1);
            $$->value.type = DRES_TYPE_INTEGER;
            $$->value.v.i  = $3;
            $$->next       = NULL;
             
        }
        | TOKEN_DRESVAR "=" TOKEN_DOUBLE {
            if (($$ = ALLOC(dres_local_t)) == NULL)
                YYABORT;
            $$->id         = dres_dresvar_id(dres, $1);
            $$->value.type = DRES_TYPE_DOUBLE;
            $$->value.v.d  = $3;
            $$->next       = NULL;

            if ($$->id == DRES_ID_NONE)
                YYABORT;
        }
        | TOKEN_DRESVAR "=" TOKEN_STRING {
            if (($$ = ALLOC(dres_local_t)) == NULL)
                YYABORT;
            $$->id         = dres_dresvar_id(dres, $1);
            $$->value.type = DRES_TYPE_STRING;
            $$->value.v.s  = STRDUP($3);
            $$->next       = NULL;
             
            if ($$->id == DRES_ID_NONE)
                YYABORT;
        }
        | TOKEN_DRESVAR "=" TOKEN_IDENT {
            if (($$ = ALLOC(dres_local_t)) == NULL)
                YYABORT;
            $$->id         = dres_dresvar_id(dres, $1);
            $$->value.type = DRES_TYPE_STRING;
            $$->value.v.s  = STRDUP($3);
            $$->next       = NULL;             

            if ($$->id == DRES_ID_NONE)
                YYABORT;
        }
        | TOKEN_DRESVAR "=" TOKEN_DRESVAR {
            if (($$ = ALLOC(dres_local_t)) == NULL)
                YYABORT;
            $$->id         = dres_dresvar_id(dres, $1);
            $$->value.type = DRES_TYPE_DRESVAR;
            $$->value.v.id = dres_dresvar_id(dres, $3);
            $$->next       = NULL;             

            if ($$->id == DRES_ID_NONE || $$->value.v.id == DRES_ID_NONE)
                YYABORT;
        }
        ;


%%


/********************
 * set_prefix
 ********************/
int
set_prefix(char *prefix)
{
    if (current_prefix != NULL)
        FREE(current_prefix);
    current_prefix = STRDUP(prefix);
    
    return current_prefix == NULL ? ENOMEM : 0;
}

/********************
 * fqfn
 ********************/
char *
factname(char *name)
{
    static char  buf[256];
    char        *prefix = current_prefix;

    snprintf(buf, sizeof(buf), "%s%s%s",
             prefix ? prefix : "", prefix ? "." : "", name);

#if 0
    printf("*** %s => %s\n", name, buf);
#endif

    return buf;
}



#ifdef __TEST_PARSER__	
void yyerror(dres_t *dres, const char *msg)
{
  printf("parse error: %s (%s)\n", msg, yylval.string);
}


int main(int argc, char *argv[])
{
  yyin = argc > 1 ? fopen(argv[1], "r") : stdin;

  yyparse(NULL);
  
  return 0;

}

#endif /* __TEST_PARSER__ */


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
