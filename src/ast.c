/******************************************************************************/
/*  This file is part of dres the resource policy dependency resolver.        */
/*                                                                            */
/*  Copyright (C) 2010 Nokia Corporation.                                     */
/*                                                                            */
/*  This library is free software; you can redistribute                       */
/*  it and/or modify it under the terms of the GNU Lesser General Public      */
/*  License as published by the Free Software Foundation                      */
/*  version 2.1 of the License.                                               */
/*                                                                            */
/*  This library is distributed in the hope that it will be useful,           */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU          */
/*  Lesser General Public License for more details.                           */
/*                                                                            */
/*  You should have received a copy of the GNU Lesser General Public          */
/*  License along with this library; if not, write to the Free Software       */
/*  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  */
/*  USA.                                                                      */
/******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

#include <dres/dres.h>
#include <dres/compiler.h>
#include "dres-debug.h"


static void dump_expr(dres_t *dres, dres_expr_t *expr);
static void free_expr(dres_expr_t *expr);


static void
dump_expr_const(dres_t *dres, dres_expr_const_t *expr)
{
    (void)dres;
    
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
    case DRES_RELOP_OR:
        printf("(");
        ARG1; printf(" || "); ARG2;
        printf(")");
        break;
    case DRES_RELOP_AND:
        printf("(");
        ARG1; printf(" && "); ARG2;
        printf(")");
        break;
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


static void
free_expr_const(dres_expr_const_t *expr)
{
    if (expr->vtype == DRES_TYPE_STRING)
        FREE(expr->v.s);
    
    FREE(expr);
}


static void
free_expr_varref(dres_expr_varref_t *expr)
{
    dres_free_varref(&expr->ref);
    FREE(expr);
}


static void
free_expr_relop(dres_expr_relop_t *expr)
{
    free_expr(expr->arg1);
    free_expr(expr->arg2);
    FREE(expr);
}


static void
free_expr_call(dres_expr_call_t *expr)
{
    FREE(expr->name);
    free_expr(expr->args);
    dres_free_locals(expr->locals);
    FREE(expr);
}


void
free_expr(dres_expr_t *expr)
{
    dres_expr_t *next;

    while (expr != NULL) {
        next = expr->any.next;

        switch (expr->type) {
        case DRES_EXPR_CONST:  free_expr_const(&expr->constant); break;
        case DRES_EXPR_VARREF: free_expr_varref(&expr->varref);  break;
        case DRES_EXPR_RELOP:  free_expr_relop(&expr->relop);    break;
        case DRES_EXPR_CALL:   free_expr_call(&expr->call);      break;
        default:
            printf("%s: error: <unknown expression type 0x%x>", __FUNCTION__,
                   expr->type);
            return;
        }

        expr = next;
    }
}


void
dres_free_expr(dres_expr_t *expr)
{
    free_expr(expr);
}


static void
free_call(dres_stmt_call_t *stmt)
{
    FREE(stmt->name);
    free_expr(stmt->args);
    dres_free_locals(stmt->locals);
    FREE(stmt);
}


static void
free_ifthen(dres_stmt_if_t *stmt)
{
    free_expr(stmt->condition);
    dres_free_statement(stmt->if_branch);
    dres_free_statement(stmt->else_branch);
    FREE(stmt);
}


static void
free_assign(dres_stmt_assign_t *stmt)
{
    free_expr_varref(stmt->lvalue);
    free_expr(stmt->rvalue);
    FREE(stmt);
}


EXPORTED void
dres_free_statement(dres_stmt_t *stmt)
{
    dres_stmt_t *next;
    
    while (stmt != NULL) {
        next = stmt->any.next;
        
        switch (stmt->type) {
        case DRES_STMT_FULL_ASSIGN:
        case DRES_STMT_PARTIAL_ASSIGN:
            free_assign(&stmt->assign);
            break;
        case DRES_STMT_CALL:
            free_call(&stmt->call);
            break;
        case DRES_STMT_IFTHEN:
            free_ifthen(&stmt->ifthen);
            break;
        default:
            printf("%s: error: unknown statement of type 0x%x.", __FUNCTION__,
                   stmt->type);
            return;
        }

        stmt = next;
    }
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
