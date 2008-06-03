%{

#include <stdio.h>
#include <string.h>

#include <dres/dres.h>

#if !defined(DEBUG)
#  if defined(__TEST_PARSER__) || 1
#    define DEBUG(fmt, args...) printf("[parser] "fmt"\n", ## args)
#  else
#    define DEBUG(fmt, args...)
#  endif
#endif


int  yylex  (void);
void yyerror(dres_t *dres, char const *);

extern FILE *yyin;

%}


%union {
  int             integer;
  char           *string;
  dres_target_t  *target;
  dres_prereq_t  *prereq;
  dres_action_t  *action;
  dres_varref_t   varref;
#if 1
  dres_assign_t   assign;
#else
  struct {
    int var;
    int val;
  } assign;
#endif
  int             argument;
  dres_action_t   variables;
  dres_action_t   arguments;
}

%defines
%parse-param {dres_t *dres}

%token <string> TOKEN_PREFIX
%token <string> TOKEN_VARNAME
%token <string> TOKEN_INITIALIZER
%token <string> TOKEN_SELECTOR
%token <string> TOKEN_IDENT
%token          TOKEN_DOT "."
%token <string> TOKEN_NUMBER
%token <string> TOKEN_FACTVAR
%token <string> TOKEN_DRESVAR
%token          TOKEN_COLON ":"
%token          TOKEN_PAREN_OPEN "("
%token          TOKEN_PAREN_CLOSE ")"
%token          TOKEN_COMMA ","
%token          TOKEN_EQUAL   "="
%token          TOKEN_APPEND "+="
%token          TOKEN_TAB "\t"
%token          TOKEN_EOL
%token          TOKEN_EOF
%token          TOKEN_UNKNOWN

%type <target>  rule
%type <integer> prereq
%type <prereq>  prereqs
%type <prereq>  optional_prereqs
%type <action>  optional_actions
%type <action>  actions
%type <action>  action
%type <action>  expr
%type <action>  call
%type <integer> value
%type <varref>  lvalue
%type <varref>  rvalue
%type <varref>  varref
%type <arguments> arguments
%type <argument>  argument
%type <variables> optional_locals
%type <variables> locals
%type <assign>    local
%%


input: optional_prefix optional_init rules

optional_prefix: /* empty */ {
              dres_set_prefix(dres, NULL);
          }
        | TOKEN_PREFIX "=" TOKEN_VARNAME TOKEN_EOL {
              dres_set_prefix(dres, $3);
          }
	;

optional_init: /* empty */
        |      initializers
        ;

initializers: initializer
        |     initializers initializer
        ;

initializer: TOKEN_FACTVAR assign_op TOKEN_INITIALIZER TOKEN_EOL {
            void *value = dres_fact_create($1, $3);

            if (value == NULL) {
	        yyerror(dres, "failed to create fact variable");
	        YYABORT;
	    }
	    
	    dres_var_create(dres->fact_store, $1, value);
	    g_object_unref(value);
        }
        ;

assign_op: "=" | "+=";

rules:    rule
	| rules rule
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

prereq:   TOKEN_IDENT            { $$ = dres_target_id(dres, $1);     }
	| TOKEN_FACTVAR          { $$ = dres_factvar_id(dres, $1);   }
	;

optional_actions: /* empty */    { $$ = NULL; }
	| actions                { $$ = $1;   }
	;

actions:  action                 { $$ = $1;   }
	| actions action         {
             dres_action_t *a;                    /* find the last action */
             for (a = $1; a->next; a = a->next)
                 ;
             a->next = $2;
             $$ = $1;
        }
	;

action: TOKEN_TAB expr TOKEN_EOL { $$ = $2; }
	;

expr:   lvalue "=" call {
            $$         = $3;
	    $$->lvalue = $1;
        }
      | lvalue "=" rvalue {
            $$ = dres_new_action(DRES_ID_NONE);
	    $$->lvalue = $1;
	    $$->rvalue = $3;
            $$->name   = STRDUP("__assign");
	}
      | call {
            $$ = $1;
        }
      | lvalue "=" TOKEN_IDENT {
            $$ = dres_new_action(DRES_ID_NONE);
	    $$->lvalue    = $1;
            $$->immediate = dres_literal_id(dres, $3); /* XXX kludge */
            $$->name      = STRDUP("__assign");
        }
      | lvalue "=" TOKEN_NUMBER {
            $$ = dres_new_action(DRES_ID_NONE);
	    $$->lvalue    = $1;
            $$->immediate = dres_literal_id(dres, $3); /* XXX kludge */
            $$->name      = STRDUP("__assign");
        }
      ;

lvalue: varref { $$ = $1; }
      ;

rvalue: varref { $$ = $1; }
      ;

varref:   TOKEN_FACTVAR {
              DEBUG("varref: %s", $1);
              $$.variable = dres_factvar_id(dres, $1);
              $$.selector = NULL;
              $$.field    = NULL;
        }
	| TOKEN_FACTVAR TOKEN_SELECTOR {
              DEBUG("varref: %s[%s]", $1, $2);
              $$.variable = dres_factvar_id(dres, $1);
              $$.selector = STRDUP($2);
              $$.field    = NULL;
        }
	| TOKEN_FACTVAR ":" TOKEN_IDENT {
              DEBUG("varref: %s:%s", $1, $3);
              $$.variable = dres_factvar_id(dres, $1);
              $$.selector = NULL;
              $$.field    = STRDUP($3);
        }
	| TOKEN_FACTVAR TOKEN_SELECTOR ":" TOKEN_IDENT {
              DEBUG("varref: %s[%s]:%s", $1, $2, $4);
              $$.variable = dres_factvar_id(dres, $1);
              $$.selector = STRDUP($2);
              $$.field    = STRDUP($4);
        }
        ;


call: TOKEN_IDENT "(" arguments optional_locals ")" {
            $$ = dres_new_action(DRES_ID_NONE);
	    $$->name = STRDUP($1);
	    $$->arguments = $3.arguments;
	    $$->nargument = $3.nargument;
#if 1
	    $$->variables = $4.variables;
	    $$->nvariable = $4.nvariable;
#endif
        }
	;

arguments: argument   {
               $$.arguments = NULL; $$.arguments = 0;
               dres_add_argument(&$$, $1);
        }
        | arguments "," argument {
               dres_add_argument(&$1, $3);
               $$ = $1;
          }
	;

argument: value            { $$ = $1; }
	| TOKEN_FACTVAR    { $$ = dres_factvar_id(dres, $1); }
	| TOKEN_DRESVAR    { $$ = dres_dresvar_id(dres, $1); }
	;


value:    TOKEN_IDENT   { $$ = dres_literal_id(dres, $1); }
	| TOKEN_NUMBER  { $$ = dres_literal_id(dres, $1); }
	;

optional_locals: /* empty */  { $$.arguments = NULL; $$.nargument = 0; }
	| "," locals          { $$ = $2; }
	;

locals:   local {
              dres_add_assignment(&$$, &$1);
        }
        | locals "," local {
              dres_add_assignment(&$1, &$3);
              $$ = $1;
        }
        ;

local:    TOKEN_DRESVAR "=" TOKEN_IDENT {
              $$.type            = DRES_ASSIGN_IMMEDIATE;
              $$.lvalue.variable = dres_dresvar_id(dres, $1);
	      $$.lvalue.selector = NULL;
	      $$.lvalue.field    = NULL;
              $$.val             = dres_literal_id(dres, $3);
        }
        | TOKEN_DRESVAR "=" TOKEN_NUMBER {
              $$.type            = DRES_ASSIGN_IMMEDIATE;
              $$.lvalue.variable = dres_dresvar_id(dres, $1);
	      $$.lvalue.selector = NULL;
	      $$.lvalue.field    = NULL;
              $$.val             = dres_literal_id(dres, $3);
        }
        | TOKEN_DRESVAR "=" TOKEN_VARNAME {
              $$.type            = DRES_ASSIGN_IMMEDIATE;
              $$.lvalue.variable = dres_dresvar_id(dres, $1);
	      $$.lvalue.selector = NULL;
	      $$.lvalue.field    = NULL;
              $$.val             = dres_literal_id(dres, $3);
        }
        | TOKEN_DRESVAR "=" varref {
              $$.type   = DRES_ASSIGN_VARIABLE;
              $$.lvalue.variable = dres_dresvar_id(dres, $1);
	      $$.lvalue.selector = NULL;
	      $$.lvalue.field    = NULL;
              $$.var             = $3;
        }
	| TOKEN_DRESVAR "=" TOKEN_DRESVAR {
              $$.type            = DRES_ASSIGN_VARIABLE;
              $$.lvalue.variable = dres_dresvar_id(dres, $1);
	      $$.lvalue.selector = NULL;
	      $$.lvalue.field    = NULL;
              $$.var.variable    = dres_dresvar_id(dres, $3);
	      $$.var.selector    = NULL;
	      $$.var.field       = NULL;
        }
        ;

%%

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
