%{

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "dres/dres.h"
#include <glib-object.h>

#if !defined(DEBUG) || defined(__TEST_PARSER__) || 1
#  define DEBUG(fmt, args...) printf("[parser] "fmt"\n", ## args)
#else
#  define DEBUG(fmt, args...)
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
    dres_field_t    field;
    dres_select_t  *select;

    dres_init_t        *init;
    dres_initializer_t *initializer;

    dres_stmt_t        *statement;
    dres_expr_t        *expression;
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
%token           TOKEN_APPEND  "+="
%token           TOKEN_PARTIAL "|="
%token           TOKEN_TAB "\t"
%token           TOKEN_EOL
%token           TOKEN_EOF

%token           TOKEN_IF   "if"
%token           TOKEN_THEN "then"
%token           TOKEN_ELSE "else"
%token           TOKEN_END  "end"
%token           TOKEN_EQ   "=="
%token           TOKEN_NE   "!="
%token           TOKEN_LT   "<"
%token           TOKEN_LE   "<="
%token           TOKEN_GT   ">"
%token           TOKEN_GE   ">="
%token           TOKEN_NOT  "!"

%token           TOKEN_UNKNOWN

%type <target>  rule
%type <integer> prereq
%type <prereq>  prereqs
%type <prereq>  optional_prereqs
%type <varref>  varref
%type <field>     field
%type <init>      ifields
%type <select>    sfields
%type <select>    sfield
%type <initializer> initializer
%type <initializer> initializers
%type <local>       local
%type <local>       locals

%type <statement>   optional_statements
%type <statement>   statements
%type <statement>   statement
%type <statement>   stmt_ifthen
%type <statement>   stmt_assign
%type <statement>   stmt_call
%type <expression>  expr
%type <expression>  expr_const
%type <expression>  expr_varref
%type <expression>  expr_relop
%type <expression>  expr_call
%type <expression>  args_by_value


%%


input: optional_prefix optional_initializers rules

optional_prefix: /* empty */
        |        prefix
	;

prefix: TOKEN_PREFIX "=" TOKEN_FACTNAME TOKEN_EOL {
            set_prefix($3);
        }
	| TOKEN_PREFIX "=" TOKEN_IDENT TOKEN_EOL {
            set_prefix($3);
	}

optional_initializers: { dres->initializers = NULL; }
        | initializers { dres->initializers = $1; }
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
	    p->next  = f;
            f->field = $3;
            f->next  = NULL;
            $$       = $1;
        }
        ;

sfields: sfield { $$ = $1; }
        | sfields "," sfield {
	    dres_select_t *p;
            for (p = $1; p->next; p = p->next)
                ;
	    p->next = $3;
            $$      = $1;
        }
        ;


sfield: TOKEN_IDENT ":" TOKEN_INTEGER {
	    if (($$ = ALLOC(dres_select_t)) == NULL)
	        YYABORT;
	    $$->op = DRES_OP_EQ;
            $$->field.name = STRDUP($1);
            $$->field.value.type = DRES_TYPE_INTEGER;
	    $$->field.value.v.i  = $3;
        }
        | TOKEN_IDENT ":" TOKEN_DOUBLE {
	    if (($$ = ALLOC(dres_select_t)) == NULL)
	        YYABORT;
	    $$->op = DRES_OP_EQ;
            $$->field.name = STRDUP($1);
            $$->field.value.type = DRES_TYPE_DOUBLE;
	    $$->field.value.v.d  = $3;
        }
        | TOKEN_IDENT ":" TOKEN_STRING {
	    if (($$ = ALLOC(dres_select_t)) == NULL)
	        YYABORT;
	    $$->op = DRES_OP_EQ;
            $$->field.name = STRDUP($1);
            $$->field.value.type = DRES_TYPE_STRING;
            $$->field.value.v.s  = STRDUP($3);
        }
        | TOKEN_IDENT ":" TOKEN_IDENT {
	    if (($$ = ALLOC(dres_select_t)) == NULL)
	        YYABORT;
	    $$->op = DRES_OP_EQ;
            $$->field.name = STRDUP($1);
            $$->field.value.type = DRES_TYPE_STRING;
            $$->field.value.v.s  = STRDUP($3);
        }
	| TOKEN_IDENT {
	    if (($$ = ALLOC(dres_select_t)) == NULL)
	        YYABORT;
	    $$->op = DRES_OP_UNKNOWN;
            $$->field.name = STRDUP($1);
	    $$->field.value.type = DRES_TYPE_UNKNOWN;
	    $$->field.value.v.i  = 0;
	}
	| TOKEN_IDENT ":" "!" TOKEN_INTEGER {
	    if (($$ = ALLOC(dres_select_t)) == NULL)
	        YYABORT;
	    $$->op = DRES_OP_NEQ;
            $$->field.name = STRDUP($1);
            $$->field.value.type = DRES_TYPE_INTEGER;
	    $$->field.value.v.i  = $4;
        }
        | TOKEN_IDENT ":" "!" TOKEN_DOUBLE {
	    if (($$ = ALLOC(dres_select_t)) == NULL)
	        YYABORT;
	    $$->op = DRES_OP_NEQ;
            $$->field.name = STRDUP($1);
            $$->field.value.type = DRES_TYPE_DOUBLE;
	    $$->field.value.v.d  = $4;
        }
        | TOKEN_IDENT ":" "!" TOKEN_STRING {
	    if (($$ = ALLOC(dres_select_t)) == NULL)
	        YYABORT;
	    $$->op = DRES_OP_NEQ;
            $$->field.name = STRDUP($1);
            $$->field.value.type = DRES_TYPE_STRING;
            $$->field.value.v.s  = STRDUP($4);
        }
        | TOKEN_IDENT ":" "!" TOKEN_IDENT {
	    if (($$ = ALLOC(dres_select_t)) == NULL)
	        YYABORT;
	    $$->op = DRES_OP_NEQ;
            $$->field.name = STRDUP($1);
            $$->field.value.type = DRES_TYPE_STRING;
            $$->field.value.v.s  = STRDUP($4);
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
        | TOKEN_IDENT ":" TOKEN_IDENT {
            $$.name = STRDUP($1);
            $$.value.type = DRES_TYPE_STRING;
            $$.value.v.s  = STRDUP($3);
        }
        | TOKEN_IDENT ":" {
            $$.name = STRDUP($1);
            $$.value.type = DRES_TYPE_STRING;
            $$.value.v.s  = STRDUP("");
        }
	| TOKEN_IDENT {
	    $$.name = STRDUP($1);
	    $$.value.type = DRES_TYPE_UNKNOWN;
	    $$.value.v.i  = 0;
	}
        ;

rules:    rule
	| rules rule
        | rules prefix
	;


rule: TOKEN_IDENT ":" optional_prereqs TOKEN_EOL optional_statements {
            dres_target_t *t = dres_lookup_target(dres, $1);
            t->prereqs = $3;
            t->statements = $5;
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

varref: TOKEN_FACTVAR {
            $$.variable = dres_factvar_id(dres, FQFN($1));
            $$.selector = NULL;
            $$.field    = NULL;
        }
        | TOKEN_FACTVAR ":" TOKEN_IDENT {
            $$.variable = dres_factvar_id(dres, FQFN($1));
            $$.selector = NULL;
            $$.field    = STRDUP($3);
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



optional_statements: /* empty */ { $$ = NULL; }
	| statements             { $$ = $1;   }
	;

statements: statement            { $$ = $1; }
	|   statements statement {
            dres_stmt_t *s;

            for (s = $1; s->any.next; s = s->any.next)
                ;

            s->any.next = $2;
            $$          = $1;
        }
	;

statement: TOKEN_TAB stmt_ifthen TOKEN_EOL { $$ = $2; }
        |  TOKEN_TAB stmt_assign TOKEN_EOL { $$ = $2; }
        |  TOKEN_TAB stmt_call   TOKEN_EOL { $$ = $2; }
        ;


stmt_ifthen: TOKEN_IF expr TOKEN_THEN TOKEN_EOL 
                      statements TOKEN_TAB TOKEN_END {
             dres_stmt_if_t *stmt = ALLOC(typeof(*stmt));
             if (stmt == NULL)
                 YYABORT;

             stmt->type        = DRES_STMT_IFTHEN;
             stmt->condition   = $2;
             stmt->if_branch   = $5;
             stmt->else_branch = NULL;

             $$ = (dres_stmt_t *)stmt;
        }
        | TOKEN_IF expr TOKEN_THEN TOKEN_EOL 
                   statements TOKEN_TAB TOKEN_ELSE TOKEN_EOL 
                   statements TOKEN_TAB TOKEN_END {
             dres_stmt_if_t *stmt = ALLOC(typeof(*stmt));
             if (stmt == NULL)
                 YYABORT;

             stmt->type        = DRES_STMT_IFTHEN;
             stmt->condition   = $2;
             stmt->if_branch   = $5;
             stmt->else_branch = $9;

             $$ = (dres_stmt_t *)stmt;
        }
        ;

stmt_assign: varref "=" expr {
            dres_stmt_assign_t *a;
            dres_expr_varref_t *vr;

            if ((a = ALLOC(typeof(*a))) == NULL)
                YYABORT;

            if ((vr = ALLOC(typeof(*vr))) == NULL) {
	        dres_free_statement((dres_stmt_t *)a);
		YYABORT;
            }

	    vr->type = DRES_EXPR_VARREF;
            vr->ref  = $1;

            a->type   = DRES_STMT_FULL_ASSIGN;
	    a->lvalue = vr;
            a->rvalue = $3;

            $$ = (dres_stmt_t *)a;
        }
        |   varref "|=" expr {
            dres_stmt_assign_t *a;
            dres_expr_varref_t *vr;

            if ((a = ALLOC(typeof(*a))) == NULL)
                YYABORT;

	    if ((vr = ALLOC(typeof(*vr))) == NULL) {
	        dres_free_statement((dres_stmt_t *)a);
		YYABORT;
            }

	    vr->type = DRES_EXPR_VARREF;
            vr->ref  = $1;

            a->type   = DRES_STMT_PARTIAL_ASSIGN;
	    a->lvalue = vr;
            a->rvalue = $3;

            $$ = (dres_stmt_t *)a;
        }
        ;

stmt_call: TOKEN_IDENT "(" args_by_value "," locals ")" {
            dres_stmt_call_t *call = ALLOC(typeof(*call));
            int               status;

            if (call == NULL)
                YYABORT;

            call->type   = DRES_STMT_CALL;
            call->name   = STRDUP($1);
            call->args   = $3;
	    call->locals = $5;

	    status = dres_register_handler(dres, call->name, NULL);
	    if (status != 0 && status != EEXIST) {
	        dres_free_statement((dres_stmt_t *)call);
	        YYABORT;
            }

            $$ = (dres_stmt_t *)call;
        }
        | TOKEN_IDENT "(" args_by_value ")" {
            dres_stmt_call_t *call = ALLOC(typeof(*call));
	    int               status;

            if (call == NULL)
                YYABORT;

            call->type   = DRES_STMT_CALL;
            call->name   = STRDUP($1);
            call->args   = $3;
	    call->locals = NULL;

	    status = dres_register_handler(dres, call->name, NULL);
	    if (status != 0 && status != EEXIST) {
	        dres_free_statement((dres_stmt_t *)call);
	        YYABORT;
            }

            $$ = (dres_stmt_t *)call;
        }
        | TOKEN_IDENT "(" locals ")" {
            dres_stmt_call_t *call = ALLOC(typeof(*call));
	    int               status;

            if (call == NULL)
                YYABORT;

            call->type   = DRES_STMT_CALL;
            call->name   = STRDUP($1);
            call->args   = NULL;
	    call->locals = $3;

	    status = dres_register_handler(dres, call->name, NULL);
	    if (status != 0 && status != EEXIST) {
	        dres_free_statement((dres_stmt_t *)call);
	        YYABORT;
            }

            $$ = (dres_stmt_t *)call;
        }
	| TOKEN_IDENT "(" ")" {
            dres_stmt_call_t *call = ALLOC(typeof(*call));
	    int               status;

            if (call == NULL)
                YYABORT;

            call->type   = DRES_STMT_CALL;
            call->name   = STRDUP($1);
            call->args   = NULL;
	    call->locals = NULL;

	    status = dres_register_handler(dres, call->name, NULL);
	    if (status != 0 && status != EEXIST) {
	        dres_free_statement((dres_stmt_t *)call);
	        YYABORT;
            }

            $$ = (dres_stmt_t *)call;
        }

args_by_value: expr { $$ = $1; }
     | args_by_value "," expr {
         dres_expr_t *arg;

         for (arg = $1; arg->any.next; arg = arg->any.next)
             ;

         arg->any.next = $3;
         $$ = $1;
     }
     ;


expr:  expr_const  { $$ = $1; }
     | expr_varref { $$ = $1; }
     | expr_relop  { $$ = $1; }
     | expr_call   { $$ = $1; }
     ;

expr_const: TOKEN_INTEGER {
                dres_expr_const_t *c = ALLOC(typeof(*c));

                if (c == NULL)
                    YYABORT;

                c->type  = DRES_EXPR_CONST;
		c->vtype = DRES_TYPE_INTEGER;
                c->v.i   = $1;

                $$ = (dres_expr_t *)c;
            }
            | TOKEN_DOUBLE {
                dres_expr_const_t *c = ALLOC(typeof(*c));

                if (c == NULL)
                    YYABORT;

                c->type  = DRES_EXPR_CONST;
		c->vtype = DRES_TYPE_DOUBLE;
                c->v.d   = $1;

                $$ = (dres_expr_t *)c;
            }
            | TOKEN_STRING {
                dres_expr_const_t *c = ALLOC(typeof(*c));

                if (c == NULL)
                    YYABORT;

                c->type  = DRES_EXPR_CONST;
		c->vtype = DRES_TYPE_STRING;
                c->v.s   = STRDUP($1);

                $$ = (dres_expr_t *)c;
            }
            | TOKEN_IDENT {
                dres_expr_const_t *c = ALLOC(typeof(*c));

                if (c == NULL)
                    YYABORT;

                c->type  = DRES_EXPR_CONST;
		c->vtype = DRES_TYPE_STRING;
                c->v.s   = STRDUP($1);

                $$ = (dres_expr_t *)c;
            }
            ;

expr_varref: varref {
                dres_expr_varref_t *vr = ALLOC(typeof(*vr));

                if (vr == NULL)
                    YYABORT;

                vr->type = DRES_EXPR_VARREF;
                vr->ref  = $1;

                $$ = (dres_expr_t *)vr;
            }
            | TOKEN_DRESVAR {
                dres_expr_varref_t *vr = ALLOC(typeof(*vr));

                if (vr == NULL)
                    YYABORT;

                vr->type         = DRES_EXPR_VARREF;
		vr->ref.variable = dres_dresvar_id(dres, $1);

                $$ = (dres_expr_t *)vr;
            }
            ;

expr_relop: expr "<" expr {
                dres_expr_relop_t *op = ALLOC(typeof(*op));

                if (op == NULL)
                    YYABORT;

                op->type = DRES_EXPR_RELOP;
                op->op   = DRES_RELOP_LT;
                op->arg1 = $1;
                op->arg2 = $3;

                $$ = (dres_expr_t *)op;
            }
            | expr "<=" expr {
                dres_expr_relop_t *op = ALLOC(typeof(*op));

                if (op == NULL)
                    YYABORT;

                op->type = DRES_EXPR_RELOP;
                op->op   = DRES_RELOP_LE;
                op->arg1 = $1;
                op->arg2 = $3;

                $$ = (dres_expr_t *)op;
            }
            | expr ">" expr {
                dres_expr_relop_t *op = ALLOC(typeof(*op));

                if (op == NULL)
                    YYABORT;

                op->type = DRES_EXPR_RELOP;
                op->op   = DRES_RELOP_GT;
                op->arg1 = $1;
                op->arg2 = $3;

                $$ = (dres_expr_t *)op;
            }
            | expr ">=" expr {
                dres_expr_relop_t *op = ALLOC(typeof(*op));

                if (op == NULL)
                    YYABORT;

                op->type = DRES_EXPR_RELOP;
                op->op   = DRES_RELOP_GE;
                op->arg1 = $1;
                op->arg2 = $3;

                $$ = (dres_expr_t *)op;
            }
            | expr "==" expr {
                dres_expr_relop_t *op = ALLOC(typeof(*op));

                if (op == NULL)
                    YYABORT;

                op->type = DRES_EXPR_RELOP;
                op->op   = DRES_RELOP_EQ;
                op->arg1 = $1;
                op->arg2 = $3;

                $$ = (dres_expr_t *)op;
            }
            | expr "!=" expr {
                dres_expr_relop_t *op = ALLOC(typeof(*op));

                if (op == NULL)
                    YYABORT;

                op->type = DRES_EXPR_RELOP;
                op->op   = DRES_RELOP_NE;
                op->arg1 = $1;
                op->arg2 = $3;

                $$ = (dres_expr_t *)op;
            }
            | "!" expr {
                dres_expr_relop_t *op = ALLOC(typeof(*op));

                if (op == NULL)
                    YYABORT;

                op->type = DRES_EXPR_RELOP;
                op->op   = DRES_RELOP_NOT;
                op->arg1 = $2;
                op->arg2 = NULL;

                $$ = (dres_expr_t *)op;
            }
            ;


expr_call: TOKEN_IDENT "(" args_by_value "," locals ")" {
            dres_expr_call_t *call = ALLOC(typeof(*call));
	    int               status;

            if (call == NULL)
                YYABORT;

            call->type    = DRES_EXPR_CALL;
            call->name    = STRDUP($1);
            call->args    = $3;
	    call->locals  = $5;

	    status = dres_register_handler(dres, call->name, NULL);
	    if (status != 0 && status != EEXIST) {
	        dres_free_expr((dres_expr_t *)call);
	        YYABORT;
            }

            $$ = (dres_expr_t *)call;
        }
        | TOKEN_IDENT "(" args_by_value ")" {
            dres_expr_call_t *call = ALLOC(typeof(*call));
	    int               status;

            if (call == NULL)
                YYABORT;

            call->type    = DRES_EXPR_CALL;
            call->name    = STRDUP($1);
            call->args    = $3;
	    call->locals  = NULL;

	    status = dres_register_handler(dres, call->name, NULL);
	    if (status != 0 && status != EEXIST) {
	        dres_free_expr((dres_expr_t *)call);
	        YYABORT;
            }

            $$ = (dres_expr_t *)call;
        }
        | TOKEN_IDENT "(" locals ")" {
            dres_expr_call_t *call = ALLOC(typeof(*call));
	    int               status;

            if (call == NULL)
                YYABORT;

            call->type    = DRES_EXPR_CALL;
            call->name    = STRDUP($1);
            call->args    = NULL;
	    call->locals  = $3;

	    status = dres_register_handler(dres, call->name, NULL);
	    if (status != 0 && status != EEXIST) {
	        dres_free_expr((dres_expr_t *)call);
	        YYABORT;
            }

            $$ = (dres_expr_t *)call;
        }
	| TOKEN_IDENT "(" ")" {
            dres_expr_call_t *call = ALLOC(typeof(*call));
	    int               status;

            if (call == NULL)
                YYABORT;

            call->type    = DRES_EXPR_CALL;
            call->name    = STRDUP($1);
            call->args    = NULL;
	    call->locals  = NULL;

	    status = dres_register_handler(dres, call->name, NULL);
	    if (status != 0 && status != EEXIST) {
	        dres_free_expr((dres_expr_t *)call);
	        YYABORT;
            }

            $$ = (dres_expr_t *)call;
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
 * factname
 ********************/
char *
factname(char *name)
{
    static char  buf[256];
    char        *prefix = current_prefix;

    /*
     * Notes:
     *     Although notation-wise this is counterintuitive because of
     *     the overloaded use of '.' we do have filesystem pathname-like
     *     conventions here. Absolute variable names start with a dot,
     *     relative variable names do not. The leading dot is removed
     *     from absolute names. Relative names get prefixed with the
     *     current prefix if any.
     *
     *     The other and perhaps more intuitive alternative would be to
     *     have it the other way around and prefix any variable names
     *     starting with a dot with the current prefix.

     *     Since the absolute/relative notation is backward-compatible
     *     with our original concept of a single default prefix we use
     *     that one.
     */

    if (name[0] != '.' && prefix && prefix[0]) {
        snprintf(buf, sizeof(buf), "%s.%s", prefix, name);
        name = buf;
    }
    else if (name[0] == '.')
        return name + 1;

    return name;
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
