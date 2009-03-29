#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

#include <dres/dres.h>
#include <dres/compiler.h>
#include "dres-debug.h"


static void dump_expr(dres_t *dres, dres_expr_t *expr);


static void
dump_expr_const(dres_t *dres, dres_expr_const_t *expr)
{
    switch (expr->vtype) {
    case DRES_TYPE_INTEGER: printf("%d", expr->v.i); break;
    case DRES_TYPE_DOUBLE:  printf("%f", expr->v.d); break;
    case DRES_TYPE_STRING:  printf("%s", expr->v.s); break;
    default: printf("<constant of unknown type 0x%x>", expr->vtype);
    }
}


static void
dump_expr_varref(dres_t *dres, dres_expr_varref_t *expr)
{
    char varref[1024];
    dres_print_varref(dres, &expr->ref, varref, sizeof(varref));
    printf("%s", varref);
}


static void
dump_expr_relop(dres_t *dres, dres_expr_relop_t *expr)
{
#define ARG1 dump_expr(dres, expr->arg1)
#define ARG2 dump_expr(dres, expr->arg2)

    switch (expr->op) {
    case DRES_RELOP_EQ:  ARG1; printf(" == "); ARG2; break;
    case DRES_RELOP_NE:  ARG1; printf(" != "); ARG2; break;
    case DRES_RELOP_LT:  ARG1; printf(" < "); ARG2;  break;
    case DRES_RELOP_LE:  ARG1; printf(" <= "); ARG2; break;
    case DRES_RELOP_GT:  ARG1; printf(" > "); ARG2;  break;
    case DRES_RELOP_GE:  ARG1; printf(" >= "); ARG2; break;
    case DRES_RELOP_NOT: printf("!"); ARG1;          break;
    default: printf("<unknown relop of type 0x%x>", expr->op);
    }
}


static void
dump_expr_call(dres_t *dres, dres_expr_call_t *call)
{
    dres_expr_t *expr;
    const char  *t;
    char         locals[1024];

    printf("%s(", call->name);

    t = "";
    for (expr = call->args; expr != NULL; expr = expr->any.next) {
        printf("%s", t);
        dump_expr(dres, expr);
        t = ", ";
    }
    if (call->locals != NULL) {
        dres_print_locals(dres, call->locals, locals, sizeof(locals));
        printf("%s%s", t, locals);
    }
    
    printf(")");
}


static void
dump_expr(dres_t *dres, dres_expr_t *expr)
{
    switch (expr->type) {
    case DRES_EXPR_CONST:  dump_expr_const(dres, &expr->constant); break;
    case DRES_EXPR_VARREF: dump_expr_varref(dres, &expr->varref);  break;
    case DRES_EXPR_RELOP:  dump_expr_relop(dres, &expr->relop);    break;
    case DRES_EXPR_CALL:   dump_expr_call(dres, &expr->call);      break;
    default: printf("<unknown expression of type 0x%x>", expr->type);
    }
}


static void
dump_assign(dres_t *dres, dres_stmt_assign_t *stmt, const char *op, int level)
{
    char varref[256];
    
    dres_print_varref(dres, &stmt->lvalue->ref, varref, sizeof(varref));
    printf("%*s%s %s ", level, "", varref, op);
    dump_expr(dres, stmt->rvalue);
    printf("\n");
}


static void
dump_call(dres_t *dres, dres_stmt_call_t *call, int level)
{
    dres_expr_t *expr;
    const char  *t;
    char         locals[1024];

    printf("%*s%s(", level, "", call->name);

    t = "";
    for (expr = call->args; expr != NULL; expr = expr->any.next) {
        printf("%s", t);
        dump_expr(dres, expr);
        t = ", ";
    }
    if (call->locals != NULL) {
        dres_print_locals(dres, call->locals, locals, sizeof(locals));
        printf("%s%s", t, locals);
    }
    
    printf(")\n");
}


static void
dump_ifthen(dres_t *dres, dres_stmt_if_t *stmt, int level)
{
    dres_stmt_t *st;
    
    printf("%*sif ", level, "");
    dump_expr(dres, stmt->condition);
    printf(" then\n");
    
    for (st = stmt->if_branch; st != NULL; st = st->any.next)
        dres_dump_statement(dres, st, level + 4);
    
    if (stmt->else_branch != NULL) {
        printf("%*selse\n", level, "");
        for (st = stmt->else_branch; st != NULL; st = st->any.next)
            dres_dump_statement(dres, st, level + 4);
    }
    
    printf("%*send\n", level, "");
}


EXPORTED void
dres_dump_statement(dres_t *dres, dres_stmt_t *stmt, int level)
{
    switch (stmt->type) {
    case DRES_STMT_FULL_ASSIGN:
        dump_assign(dres, &stmt->assign, "=", level);
        break;
    case DRES_STMT_PARTIAL_ASSIGN:
        dump_assign(dres, &stmt->assign, "|=", level);
        break;
    case DRES_STMT_CALL:
        dump_call(dres, &stmt->call, level);
        break;
    case DRES_STMT_IFTHEN:
        dump_ifthen(dres, &stmt->ifthen, level);
        break;
    default: printf("%*s<unknown statement of type 0x%x>\n",
                    level, "", stmt->type);
    }
}





/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
