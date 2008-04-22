%{

#include <stdio.h>
#include "dres.h"

#ifdef __TEST_PARSER__
#  define DEBUG(c, fmt, args...) printf("["#c"] "fmt"\n", ## args)
#else
#  define DEBUG(c, fmt, args...)
#endif


int  yylex  (void);
void yyerror(char const *);

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
                                 dres_target_t *t = dres_lookup_target($1);
                                 t->prereqs = $3;
                                 t->actions = $5;
                                 t->id      = DRES_DEFINED(t->id);

                                 $$ = t;
                              }
	 ;

optional_prereqs: /* empty */    { $$ = NULL;  }
	| prereqs                { $$ = $1;    }
	;

prereqs:  prereq                 { $$ = dres_new_prereq($1);         }
	| prereqs prereq         { dres_add_prereq($1, $2); $$ = $1; }
	;

prereq:   TOKEN_IDENT            { $$ = dres_target_id($1);   }
	| TOKEN_FACTVAR          { $$ = dres_variable_id($1); }
	| TOKEN_DRESVAR          { $$ = dres_variable_id($1); /*XXX kludge*/ }
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

action: TOKEN_TAB optional_lvalue 
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

optional_lvalue: /* empty */        { $$ = DRES_ID_NONE;         }
        | TOKEN_FACTVAR TOKEN_EQUAL { $$ = dres_variable_id($1); }
        ;

arguments: argument   {
               $$.arguments = NULL; $$.arguments = 0;
               dres_add_argument(&$$, $1);
               /*printf("### added first argument 0x%x\n", $1);*/
        }
        |  arguments TOKEN_COMMA argument {
                dres_add_argument(&$1, $3);
                $$ = $1;
                /*printf("### appended argument 0x%x\n", $3);*/
           }
	;

argument: value            { $$ = $1; }
	| TOKEN_FACTVAR    { $$ = dres_variable_id($1); }
	| TOKEN_DRESVAR    { $$ = dres_variable_id($1); /* XXX kludge */ }
	;


optional_assignments: /* empty */  {
            $$.arguments = NULL;
            $$.nargument = 0;
            /*printf("### no arguments\n");*/
        }
	| TOKEN_COMMA assignments {
            $$ = $2;
            /*printf("### had assignments\n");*/
        }
	;

assignments: assignment {
            dres_add_assignment(&$$, $1.var, $1.val);
            /*printf("### added first assignment 0x%x = 0x%x\n", $1.var, $1.val);*/
        }
        | assignments TOKEN_COMMA assignment {
            dres_add_assignment(&$1, $3.var, $3.val);
            $$ = $1;
            /*printf("### appended assignment 0x%x = 0x%x\n", $3.var, $3.val);*/
        }
        ;

assignment: TOKEN_DRESVAR TOKEN_EQUAL value {
	    char buf[32];
	    $$.var = dres_variable_id($1);   /* XXX kludge */
            $$.val = $3;
            /*printf("### $%s = %s\n", $1, dres_name($3, buf, sizeof(buf)));*/
        }
	;

value: TOKEN_IDENT                  { $$ = dres_literal_id($1); }
	| TOKEN_NUMBER              { $$ = dres_literal_id($1); }
	| TOKEN_FACTVAR TOKEN_DOT TOKEN_IDENT {
              $$ = dres_literal_id($3); /* XXX kludge */
        }
        | TOKEN_DRESVAR TOKEN_DOT TOKEN_IDENT {
              $$ = dres_literal_id($3); /* XXX kludge */
        }
	;



%%

#ifdef __TEST_PARSER__	
void yyerror(const char *msg)
{
  printf("parse error: %s (%s)\n", msg, yylval.string);
}


int main(int argc, char *argv[])
{
  yyin = argc > 1 ? fopen(argv[1], "r") : stdin;

  yyparse();
  
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
