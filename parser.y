%{

#include <stdio.h>
#include "dres.h"

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
}

%token <string>  TOKEN_TARGET_DEF
%token <string>  TOKEN_TARGET_REF
%token <string>  TOKEN_VARIABLE_REF
%token <string>  TOKEN_ACTION_NAME
%token <string>  TOKEN_ACTION_ARG
%token           TOKEN_TAB
%token           TOKEN_COLON
%token           TOKEN_PAREN_OPEN
%token           TOKEN_PAREN_CLOSE
%token <string>  TOKEN_UNKNOWN
%token           TOKEN_EOL

%type <integer> argument
%type <integer> prereq
%type <prereq>  prereqs
%type <prereq>  optional_prereqs
%type <action>  action
%type <action>  actions
%type <action>  optional_actions
%type <integer> argument
%type <action>  arguments
%type <action>  optional_arguments
%type <target>  rule


%%


input: rules

rules:    rule
	| rules rule
	;

rule: TOKEN_TARGET_DEF TOKEN_COLON optional_prereqs optional_actions {
                                   dres_target_t *t = dres_lookup_target($1);
                                   t->prereqs = $3;
                                   t->actions = $4;
                                   t->id = DRES_DEFINED(t->id);
                                   $$ = t;
                                 }
	;

optional_prereqs: /* empty */    { $$ = NULL; }
	| prereqs                { $$ = $1;   }
	;

prereqs:  prereq                 { $$ = dres_new_prereq($1);         }
	| prereqs prereq         { dres_add_prereq($1, $2); $$ = $1; }
	;

prereq:   TOKEN_TARGET_REF       { $$ = dres_target_id($1);   }
	| TOKEN_VARIABLE_REF     { $$ = dres_variable_id($1); }
	;


optional_actions: /* empty */    { $$ = NULL; }
	| actions                { $$ = $1; }
	;

actions:  action                 { $$ = $1; }
	| actions action         {
            dres_action_t *a;
            for (a = $1; a->next; a = a->next)        /* argh... */
                ;
            a->next = $2;
            $$ = $1;
 }
	;

action: TOKEN_TAB TOKEN_ACTION_NAME 
            TOKEN_PAREN_OPEN optional_arguments TOKEN_PAREN_CLOSE {
      if ($4 == NULL)
	$$ = dres_new_action(DRES_ID_NONE);
      else {
          $$ = $4;
      }
      $$->name = $2;
 }
 ;

optional_arguments: /* empty */  { $$ = NULL; }
	| arguments              { $$ = $1; }
	;

arguments: argument              { $$ = dres_new_action($1);           }
	|  arguments argument    { dres_add_argument($1, $2); $$ = $1; }
	;

argument: TOKEN_ACTION_ARG       { $$ = dres_literal_id($1);  }
	| TOKEN_VARIABLE_REF     { $$ = dres_variable_id($1); }
	;

%%

#ifdef __TEST_PARSER__	
void yyerror(const char *msg)
{
  printf("parse error: %s (%s)\n", msg, yylval.string);
}


int main(int argc, char *argv[])
{
    char         *goal, *rulefile;
    dres_graph_t *graph;
    int          *order;


    rulefile = argc < 2 ? NULL  : argv[1];
    goal     = argc < 3 ? "all" : argv[2];
    
    if (dres_init(rulefile) != 0) {
        printf("failed to initialize dres with\n");
        return 0;
    }
        
    dres_dump_targets();
    
    if (argc > 2)
        goal = argv[2];
    else
        goal = "all";

    if ((graph = dres_build_graph(goal)) == NULL)
        printf("failed to build dependency graph for goal \"%s\"\n", goal);
    else
        printf("graph successfully built for goal \"%s\"\n", goal);
    
    if (graph) {
        if ((order = dres_sort_graph(graph)) == NULL) {
            printf("toplogical sort for goal \"%s\" failed\n", goal);
        }
        else {
            printf("topological order for goal \"%s\":\n", goal);
            dres_dump_sort(order);
            free(order);
        }
    }

    dres_update_goal(goal);

    dres_exit();
  
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
