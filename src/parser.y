%{

#include <stdio.h>
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
  struct {
    int var;
    int val;
  } assign;
  int             argument;
  dres_action_t   variables;
  dres_action_t   arguments;
}

%defines
%parse-param {dres_t *dres}


%token <string> TOKEN_IDENT
%token          TOKEN_DOT
%token <string> TOKEN_NUMBER
%token <string> TOKEN_FACTVAR
%token <string> TOKEN_DRESVAR
%token          TOKEN_COLON
%token          TOKEN_PAREN_OPEN
%token          TOKEN_PAREN_CLOSE
%token          TOKEN_COMMA
%token          TOKEN_EQUAL
%token          TOKEN_TAB
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
%type <integer> optional_lvalue
%type <integer> value

%type <arguments> arguments
%type <argument>  argument
%type <variables> optional_assignments
%type <variables> assignments
%type <assign>    assignment
%%


input: rules

rules:    rule
	| rules rule
	;


rule: TOKEN_IDENT TOKEN_COLON optional_prereqs TOKEN_EOL optional_actions {
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
	| TOKEN_DRESVAR          { $$ = dres_dresvar_id(dres, $1);   }
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

action: TOKEN_TAB
	  optional_lvalue
	  TOKEN_IDENT
          TOKEN_PAREN_OPEN arguments
                           optional_assignments TOKEN_PAREN_CLOSE TOKEN_EOL {
            $$ = dres_new_action(DRES_ID_NONE);
	    $$->name      = $3;
	    $$->lvalue    = $2;
            $$->arguments = $5.arguments;
	    $$->nargument = $5.nargument;
            $$->variables = $6.variables;
            $$->nvariable = $6.nvariable;
        }
        ;

optional_lvalue: /* empty */        { $$ = DRES_ID_NONE;              }
        | TOKEN_FACTVAR TOKEN_EQUAL { $$ = dres_factvar_id(dres, $1); }
        ;

arguments: argument   {
               $$.arguments = NULL; $$.arguments = 0;
               dres_add_argument(&$$, $1);
        }
        | arguments TOKEN_COMMA argument {
               dres_add_argument(&$1, $3);
               $$ = $1;
          }
	;

argument: value            { $$ = $1; }
	| TOKEN_FACTVAR    { $$ = dres_factvar_id(dres, $1); }
	| TOKEN_DRESVAR    { $$ = dres_dresvar_id(dres, $1); }
	;

optional_assignments: /* empty */  { $$.arguments = NULL; $$.nargument = 0; }
	| TOKEN_COMMA assignments  { $$ = $2; }
	;

assignments: assignment {
              dres_add_assignment(&$$, $1.var, $1.val);
          }
        | assignments TOKEN_COMMA assignment {
              dres_add_assignment(&$1, $3.var, $3.val);
              $$ = $1;
          }
        ;

assignment: TOKEN_DRESVAR TOKEN_EQUAL value {
	    $$.var = dres_dresvar_id(dres, $1);
            $$.val = $3;
        }
	;

value: TOKEN_IDENT                  { $$ = dres_literal_id(dres, $1); }
	| TOKEN_NUMBER              { $$ = dres_literal_id(dres, $1); }
	| TOKEN_FACTVAR TOKEN_DOT TOKEN_IDENT {
              $$ = dres_literal_id(dres, $3); /* XXX kludge */
        }
        | TOKEN_DRESVAR TOKEN_DOT TOKEN_IDENT {
              $$ = dres_literal_id(dres, $3); /* XXX kludge */
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
