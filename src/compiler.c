/*************************************************************************
This file is part of dres the resource policy dependency resolver.

Copyright (C) 2010 Nokia Corporation.

This library is free software; you can redistribute
it and/or modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation
version 2.1 of the License.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
USA.
*************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <dres/dres.h>
#include <dres/compiler.h>
#include <dres/vm.h>

#if (!defined(_XOPEN_SOURCE) && !defined(_ISOC99_SOURCE)) || _XOPEN_SOURCE < 600
double trunc(double);
#endif

static int compile_statement(dres_t *dres, dres_stmt_t *stmt, vm_chunk_t *code);
static int compile_stmt_lvalue(dres_t *dres, dres_varref_t *lval, int op,
                               vm_chunk_t *code);
static int compile_stmt_assign(dres_t *dres, dres_stmt_assign_t *stmt,
                               vm_chunk_t *code);
static int compile_stmt_call(dres_t *dres, dres_stmt_call_t *stmt,
                             vm_chunk_t *code);
static int compile_stmt_ifthen(dres_t *dres, dres_stmt_if_t *stmt,
                               vm_chunk_t *code);
static int compile_stmt_discard(dres_t *dres, vm_chunk_t *code);
static int compile_stmt_debug  (const char *info, vm_chunk_t *code);
static int compile_call(dres_t *dres, const char *method, dres_expr_t *args,
                        dres_local_t *locals, vm_chunk_t *code);
static int compile_expr(dres_t *dres, dres_expr_t *expr, vm_chunk_t *code);
static int compile_expr_const(dres_t *dres, dres_expr_const_t *expr,
                              vm_chunk_t *code);
static int compile_expr_varref(dres_t *dres, dres_expr_varref_t *expr,
                               vm_chunk_t *code);
static int compile_expr_relop(dres_t *dres, dres_expr_relop_t *expr,
                              vm_chunk_t *code);
static int compile_expr_call(dres_t *dres, dres_expr_call_t *expr,
                             vm_chunk_t *code);

static int save_initializers(dres_t *dres, dres_buf_t *buf);
static int load_initializers(dres_t *dres, dres_buf_t *buf);
static int save_methods     (dres_t *dres, dres_buf_t *buf);
static int load_methods     (dres_t *dres, dres_buf_t *buf);

extern int initialize_variables(dres_t *dres); /* XXX TODO: kludge */
extern int finalize_variables  (dres_t *dres); /* XXX TODO: kludge */



/********************
 * dres_compile_target
 ********************/
int
dres_compile_target(dres_t *dres, dres_target_t *target)
{
#if 0
    dres_action_t *a;
#endif
    dres_stmt_t   *stmt;
    int            err;

    if (target->statements == NULL)
        return 0;

    if (target->code == NULL)
        if ((target->code = vm_chunk_new(16)) == NULL)
            return ENOMEM;
    
    for (stmt = target->statements; stmt != NULL; stmt = stmt->any.next) {
        if (!compile_statement(dres, stmt, target->code)) {
            DRES_ERROR("failed to compile code for target %s:\n", target->name);
            dres_dump_statement(dres, stmt, 4);
            return EINVAL;
        }
    }

    VM_INSTR_HALT(target->code, fail, err);

    return 0;

 fail:
    DRES_ERROR("code generation for target %s failed", target->name);
    return EINVAL;
}



#define PUSH_VALUE(code, fail, err, value) do {                         \
        switch ((value)->type) {                                        \
        case DRES_TYPE_INTEGER:                                         \
            VM_INSTR_PUSH_INT((code), fail, err, (value)->v.i);         \
            break;                                                      \
        case DRES_TYPE_DOUBLE:                                          \
            VM_INSTR_PUSH_DOUBLE((code), fail, err, (value)->v.d);      \
            break;                                                      \
        case DRES_TYPE_STRING:                                          \
            VM_INSTR_PUSH_STRING((code), fail, err, (value)->v.s);      \
            break;                                                      \
        case DRES_TYPE_FACTVAR: {                                       \
            const char *__f = dres_factvar_name(dres, (value)->v.id);   \
            if (__f == NULL) {                                          \
                err = EINVAL;                                           \
                goto fail;                                              \
            }                                                           \
            VM_INSTR_PUSH_GLOBAL((code), fail, err, __f);               \
        }                                                               \
            break;                                                      \
        case DRES_TYPE_DRESVAR:                                         \
            VM_INSTR_GET_LOCAL((code), fail, err, (value)->v.id);       \
            break;                                                      \
        default:                                                        \
            err = EINVAL;                                               \
            goto fail;                                                  \
        }                                                               \
    } while (0)


#define FAIL(fmt, args...) do {                           \
        DRES_ERROR("%s: "fmt , __FUNCTION__ , ## args);   \
        goto fail;                                        \
    } while (0)


/********************
 * compile_statement
 ********************/
static int
compile_statement(dres_t *dres, dres_stmt_t *stmt, vm_chunk_t *code)
{
    switch (stmt->type) {
    case DRES_STMT_FULL_ASSIGN:
    case DRES_STMT_PARTIAL_ASSIGN:
        return compile_stmt_assign(dres, &stmt->assign, code);

    case DRES_STMT_CALL:
        return compile_stmt_call(dres, &stmt->call, code);

    case DRES_STMT_IFTHEN:
        return compile_stmt_ifthen(dres, &stmt->ifthen, code);

    default:
        DRES_ERROR("statement of unknown type 0x%x", stmt->type);
    }
    
    return FALSE;
}


static int
compile_stmt_lvalue(dres_t *dres, dres_varref_t *lval, int op, vm_chunk_t *code)
{
    const char    *name;
    dres_select_t *sel;
    int            update, nfield, partial, selop, err;


    partial = (op == DRES_STMT_PARTIAL_ASSIGN);

    name = dres_factvar_name(dres, lval->variable);
    
    if (name == NULL)
        FAIL("failed to look up global");
    
    VM_INSTR_PUSH_GLOBAL(code, fail, err, name);

    update = FALSE;
    for (nfield = 0, sel = lval->selector; sel != NULL; sel = sel->next) {
        if (sel->field.value.type == DRES_TYPE_UNKNOWN) {  /* an update */
            update = TRUE;
            continue;
        }
        else {
            selop = (int)sel->op;
            VM_INSTR_PUSH_INT(code, fail, err, selop);
            PUSH_VALUE(code, fail, err, &sel->field.value);
            VM_INSTR_PUSH_STRING(code, fail, err, sel->field.name);
            nfield++;
        }
    }
    if (nfield)
        VM_INSTR_FILTER(code, fail, err, nfield);
    
    if (partial && !update) {
        /* partial assignments without update make no sense */
        FAIL("partial assignments must be also updates");
    }


    if (update) {
        if (lval->field != NULL)
            FAIL("lvalue with a field in an update assignment");

        for (nfield = 0, sel = lval->selector; sel != NULL; sel = sel->next) {
            if (sel->field.value.type != DRES_TYPE_UNKNOWN) /* a filter */
                continue;
            VM_INSTR_PUSH_STRING(code, fail, err, sel->field.name);
            nfield++;
        }
        VM_INSTR_UPDATE(code, fail, err, nfield, partial);
    }
    else {
        if (lval->field != NULL) {
            VM_INSTR_PUSH_STRING(code, fail, err, lval->field);
            VM_INSTR_SET_FIELD(code, fail, err);
        }
        else
            VM_INSTR_SET(code, fail, err);
    }
        
    return TRUE;

 fail:
    DRES_ERROR("%s: code generation failed", __FUNCTION__);
    return FALSE;
}


static int
compile_stmt_assign(dres_t *dres, dres_stmt_assign_t *stmt, vm_chunk_t *code)
{
    if (!compile_expr(dres, stmt->rvalue, code))
        return FALSE;
    
    switch (DRES_ID_TYPE(stmt->lvalue->ref.variable)) {
    case DRES_TYPE_FACTVAR:
        return compile_stmt_lvalue(dres, &stmt->lvalue->ref, stmt->type, code);

    case DRES_TYPE_DRESVAR:
        DRES_ERROR("assignments to local variables are not supported");
        return FALSE;

    default:
        DRES_ERROR("assignment with lvalue of invalid type");
        return FALSE;
    }
}


static int
compile_stmt_call(dres_t *dres, dres_stmt_call_t *stmt, vm_chunk_t *code)
{
    if (!compile_call(dres, stmt->name, stmt->args, stmt->locals, code) ||
        !compile_stmt_discard(dres, code)) {
        DRES_ERROR("%s: code generation failed", __FUNCTION__);
        return FALSE;
    }
    else
        return TRUE;
}


static int
compile_stmt_ifthen(dres_t *dres, dres_stmt_if_t *stmt, vm_chunk_t *code)
{
    dres_stmt_t *brst;
    int          brif, brelse, brend, err;
    
    if (!compile_expr(dres, stmt->condition, code))
        FAIL("failed to generate code for if-then branching condition");
    
    brif = VM_INSTR_BRANCH(code, fail, err, VM_BRANCH_NE, 0);
    
    for (brst = stmt->if_branch; brst != NULL; brst = brst->any.next)
        if (!compile_statement(dres, brst, code))
            FAIL("failed to compile if-branch");

    if (stmt->else_branch != NULL) {
        brelse = VM_INSTR_BRANCH(code, fail, err, VM_BRANCH, 0);

        for (brst = stmt->else_branch; brst != NULL; brst = brst->any.next)
            if (!compile_statement(dres, brst, code))
                FAIL("failed to compile else-branch");
    }
    
    brend = VM_CHUNK_OFFSET(code);
    
    if (stmt->else_branch != NULL) {
        VM_BRANCH_PATCH(code, brif  , fail, err, VM_BRANCH_NE, brelse-brif+1);
        VM_BRANCH_PATCH(code, brelse, fail, err, VM_BRANCH   , brend-brelse);
    }
    else
        VM_BRANCH_PATCH(code, brif  , fail, err, VM_BRANCH_NE, brend-brif);
    
    return TRUE;

 fail:
    DRES_ERROR("%s: code generation failed", __FUNCTION__);
    return FALSE;
}


static int
compile_stmt_discard(dres_t *dres, vm_chunk_t *code)
{
    int err;

    (void)dres;
    
    VM_INSTR_POP_DISCARD(code, fail, err);
    return TRUE;

 fail:
    DRES_ERROR("%s: code generation failed", __FUNCTION__);
    return FALSE;
}


static int
compile_stmt_debug(const char *info, vm_chunk_t *code)
{
    int err;

    VM_INSTR_DEBUG(code, fail, err, info);
    return TRUE;
    
 fail:
    DRES_ERROR("%s: code generation failed for debug info \"%s\"",
               __FUNCTION__, info);
    return FALSE;
}


static int
compile_call(dres_t *dres,
             const char *method, dres_expr_t *args, dres_local_t *locals,
             vm_chunk_t *code)
{
    dres_expr_t  *arg;
    dres_local_t *local;
    int           id, narg, nlocal, err;


    id = vm_method_id(&dres->vm, (char *)method);

    if (id < 0)
        FAIL("unknown method \"%s\"", method);

    narg = 0;
    for (arg = args; arg != NULL; arg = arg->any.next) {
        if (!compile_expr(dres, arg, code))
            FAIL("failed to generate code for call argument #%d", narg);
        narg++;
    }
    
    nlocal = 0;
    for (local = locals; local != NULL; local = local->next) {
        PUSH_VALUE(code, fail, err, &local->value);
        VM_INSTR_PUSH_INT(code, fail, err, DRES_INDEX(local->id));
        nlocal++;
    }
    if (nlocal > 0)
        VM_INSTR_PUSH_LOCALS(code, fail, err, nlocal);
 
    VM_INSTR_PUSH_INT(code, fail, err, id);
    VM_INSTR_CALL(code, fail, err, narg);

    if (nlocal > 0)
        VM_INSTR_POP_LOCALS(code, fail, err);
    
    return TRUE;

 fail:
    DRES_ERROR("%s: code generation failed", __FUNCTION__);
    return FALSE;
}


static int
compile_expr(dres_t *dres, dres_expr_t *expr, vm_chunk_t *code)
{
    switch (expr->type) {
    case DRES_EXPR_CONST:
        return compile_expr_const(dres, &expr->constant, code);
    case DRES_EXPR_VARREF:
        return compile_expr_varref(dres, &expr->varref, code);
    case DRES_EXPR_RELOP:
        return compile_expr_relop(dres, &expr->relop, code);
    case DRES_EXPR_CALL:
        return compile_expr_call(dres, &expr->call, code);
    default:
        DRES_ERROR("expression with invalid type 0x%x", expr->type);
        return FALSE;
    }
}


static int
compile_expr_const(dres_t *dres, dres_expr_const_t *expr, vm_chunk_t *code)
{
    int err;
    
    (void)dres;
    
    switch (expr->vtype) {
    case DRES_TYPE_INTEGER:
        VM_INSTR_PUSH_INT(code, fail, err, expr->v.i);
        break;
    case DRES_TYPE_DOUBLE:
        VM_INSTR_PUSH_DOUBLE(code, fail, err, expr->v.d);
        break;
    case DRES_TYPE_STRING:
        VM_INSTR_PUSH_STRING(code, fail, err, expr->v.s);
        break;
    default:
        DRES_ERROR("%s: value of invalid type 0x%x", __FUNCTION__, expr->vtype);
        goto fail;
    }

    return TRUE;

 fail:
    DRES_ERROR("%s: code generation failed", __FUNCTION__);
    return FALSE;
}


static int
compile_expr_varref(dres_t *dres, dres_expr_varref_t *expr, vm_chunk_t *code)
{
    const char    *name;
    dres_varref_t *vref;
    dres_select_t *sel;
    int            nfield, op, err;


    vref = &expr->ref;

    if (DRES_ID_TYPE(vref->variable) == DRES_TYPE_DRESVAR) {
        if (vref->selector != NULL || vref->field != NULL)
            FAIL("local variables cannot have selectors or a field");
        
        VM_INSTR_GET_LOCAL(code, fail, err, vref->variable);
    }
    else {
        name = dres_factvar_name(dres, vref->variable);
    
        if (name == NULL)
            FAIL("failed to look up global 0x%x", vref->variable);
    
        VM_INSTR_PUSH_GLOBAL(code, fail, err, name);

        for (nfield = 0, sel = vref->selector; sel != NULL; sel = sel->next) {
            if (sel->field.value.type == DRES_TYPE_UNKNOWN)
                FAIL("update-stype field in a non-lvalue variable reference");

            op = (int)sel->op;
            VM_INSTR_PUSH_INT(code, fail, err, op);
            PUSH_VALUE(code, fail, err, &sel->field.value);
            VM_INSTR_PUSH_STRING(code, fail, err, sel->field.name);
            
            nfield++;
        }
        if (nfield)
            VM_INSTR_FILTER(code, fail, err, nfield);

        if (vref->field != NULL) {
            VM_INSTR_PUSH_STRING(code, fail, err, vref->field);
            VM_INSTR_GET_FIELD(code, fail, err);
        }
    }

    return TRUE;
    
 fail:
    DRES_ERROR("%s: code generation failed", __FUNCTION__);    
    return FALSE;
}


static int
compile_expr_or(dres_t *dres, dres_expr_relop_t *expr, vm_chunk_t *code)
{
    int brTX, brFT, brP1, err;

    /* evaluate arg1 */
    if (!compile_expr(dres, expr->arg1, code)) {
        DRES_ERROR("%s: code generation failed", __FUNCTION__);
        return FALSE;
    }

    /* branch to 'push 1' if arg1 was true */
    brTX = VM_INSTR_BRANCH(code, fail, err, VM_BRANCH_EQ, 0);
    
    /* evaluate arg2 */
    if (!compile_expr(dres, expr->arg2, code)) {
        DRES_ERROR("%s: code generation failed", __FUNCTION__);
        return FALSE;
    }

    /* branch to 'push 1' if arg2 was true */
    brFT = VM_INSTR_BRANCH(code, fail, err, VM_BRANCH_EQ, 0);
    
    /* push 0 and jump to end (2 instructions, the size of push + branch) */
    VM_INSTR_PUSH_INT(code, fail, err, 0);
    brP1 = VM_INSTR_BRANCH(code, fail, err, VM_BRANCH, 2);
    VM_INSTR_PUSH_INT(code, fail, err, 1);
    
    VM_BRANCH_PATCH(code, brTX, fail, err, VM_BRANCH_EQ, brP1-brTX+1);
    VM_BRANCH_PATCH(code, brFT, fail, err, VM_BRANCH_EQ, brP1-brFT+1);
    return TRUE;

 fail:
    return FALSE;
}


static int
compile_expr_and(dres_t *dres, dres_expr_relop_t *expr, vm_chunk_t *code)
{
    int brFX, brTF, brP0, err;

    /* evaluate arg1 */
    if (!compile_expr(dres, expr->arg1, code)) {
        DRES_ERROR("%s: code generation failed", __FUNCTION__);
        return FALSE;
    }

    /* branch to 'push 0' if arg1 was false */
    brFX = VM_INSTR_BRANCH(code, fail, err, VM_BRANCH_NE, 0);
    
    /* evaluate arg2 */
    if (!compile_expr(dres, expr->arg2, code)) {
        DRES_ERROR("%s: code generation failed", __FUNCTION__);
        return FALSE;
    }

    /* branch to 'push 0' if arg2 was false */
    brTF = VM_INSTR_BRANCH(code, fail, err, VM_BRANCH_NE, 0);
    
    /* push 1 and jump to end (2 instructions, the size of push + branch) */
    VM_INSTR_PUSH_INT(code, fail, err, 1);
    brP0 = VM_INSTR_BRANCH(code, fail, err, VM_BRANCH, 2);
    VM_INSTR_PUSH_INT(code, fail, err, 0);
    
    VM_BRANCH_PATCH(code, brFX, fail, err, VM_BRANCH_NE, brP0-brFX+1);
    VM_BRANCH_PATCH(code, brTF, fail, err, VM_BRANCH_NE, brP0-brTF+1);
    return TRUE;

 fail:
    return FALSE;
}


static int
compile_expr_boolean(dres_t *dres, dres_expr_relop_t *expr, vm_chunk_t *code)
{
    
    switch (expr->op) {
    case DRES_RELOP_OR:  return compile_expr_or(dres, expr, code); break;
    case DRES_RELOP_AND: return compile_expr_and(dres, expr, code); break;
    default: FAIL("invalid boolean operator 0x%x", expr->op);
    }

 fail:
    DRES_ERROR("%s: code generation failed", __FUNCTION__);
    return FALSE;
}


static int
compile_expr_relop(dres_t *dres, dres_expr_relop_t *expr, vm_chunk_t *code)
{
    int err;

    
    if (expr->op == VM_RELOP_OR || expr->op == VM_RELOP_AND)
        return compile_expr_boolean(dres, expr, code);
    else {
        if (expr->arg2)
            if (!compile_expr(dres, expr->arg2, code))
                FAIL("failed to generate code for relop argument");

        if (!compile_expr(dres, expr->arg1, code))
            FAIL("failed to generate code for relop argument");
    
        VM_INSTR_CMP(code, fail, err, expr->op);
        return TRUE;
    }

 fail:
    DRES_ERROR("%s: code generation failed", __FUNCTION__);
    return FALSE;
}


static int
compile_expr_call(dres_t *dres, dres_expr_call_t *expr, vm_chunk_t *code)
{
    return compile_call(dres, expr->name, expr->args, expr->locals, code);
}


/*****************************************************************************
 *                   *** precompiled/binary rule support ***                 *
 *****************************************************************************/


/********************
 * dres_save
 ********************/
EXPORTED int
dres_save(dres_t *dres, char *path)
{
#define INITIAL_SIZE (64 * 1024)
#define MAX_SIZE     (1024 * 1024)
    
    dres_buf_t *buf;
    int         size, status;
    FILE       *fp;
    
    size = INITIAL_SIZE;
    buf  = NULL;
    fp   = NULL;

 retry:
    if ((buf = dres_buf_create(size, size)) == NULL) {
        status = ENOMEM;
        goto fail;
    }
    
    if ((status = dres_save_targets(dres, buf)) != 0)
        goto fail;

    if ((status = dres_save_factvars(dres, buf)) != 0)
        goto fail;

    if ((status = dres_save_dresvars(dres, buf)) != 0)
        goto fail;

    if ((status = save_initializers(dres, buf)) != 0)
        goto fail;

    if ((status = save_methods(dres, buf)) != 0)
        goto fail;

    if ((fp = fopen(path, "w")) == NULL)
        goto fail;

#define HTONL(_f) buf->header._f = htonl(buf->header._f)
    buf->header.magic   = htonl(DRES_MAGIC);
    buf->header.ssize   = htonl(buf->sused);
    HTONL(ntarget);
    HTONL(nprereq);
    HTONL(ncode);
    HTONL(sinstr);
    HTONL(ndependency);
    HTONL(nvariable);
    HTONL(ninit);
    HTONL(nfield);
    HTONL(nmethod);
    
    if (fwrite(&buf->header, sizeof(buf->header), 1, fp) != 1)
        goto fail;
    
    if (fwrite(buf->strings, buf->sused, 1, fp) != 1 ||
        fwrite(buf->data   , buf->dused, 1, fp) != 1)
        goto fail;

    fclose(fp);
    
    return 0;
        
 fail:
    dres_buf_destroy(buf);

    if (status == ENOMEM && size < MAX_SIZE) {
        size *= 2;
        goto retry;
    }
    
    if (fp != NULL) {
        fclose(fp);
        unlink(path);
    }
    
    return status;
}


/********************
 * save_initializers
 ********************/
static int
save_initializers(dres_t *dres, dres_buf_t *buf)
{
    dres_initializer_t *init;
    dres_init_t        *f;
    int                 ninit, nfield;

    ninit = 0;
    for (init = dres->initializers; init != NULL; init = init->next)
        ninit++;

    dres_buf_ws32(buf, ninit);
    buf->header.ninit = ninit;

    for (init = dres->initializers; init != NULL; init = init->next) {
        dres_buf_ws32(buf, init->variable);
        
        nfield = 0;
        for (f = init->fields; f != NULL; f = f->next)
            nfield++;

        dres_buf_ws32(buf, nfield);
        for (f = init->fields; f != NULL; f = f->next) {
            dres_buf_wstr(buf, f->field.name);
            dres_buf_ws32(buf, f->field.value.type);
            
            switch (f->field.value.type) {
            case DRES_TYPE_INTEGER:
            case DRES_TYPE_DRESVAR:
                dres_buf_ws32(buf, f->field.value.v.i);
                break;
            case DRES_TYPE_STRING:
                dres_buf_wstr(buf, f->field.value.v.s);
                break;
            case DRES_TYPE_DOUBLE:
                dres_buf_wdbl(buf, f->field.value.v.d);
                break;
            }
        }

        buf->header.nfield += nfield;
    }

    return 0;
}


/********************
 * save_methods
 ********************/
static int
save_methods(dres_t *dres, dres_buf_t *buf)
{
    vm_method_t *m;
    int          i;

    dres_buf_ws32(buf, dres->vm.nmethod);
    buf->header.nmethod = dres->vm.nmethod;

    for (i = 0, m = dres->vm.methods; i < dres->vm.nmethod; i++, m++) {
        dres_buf_ws32(buf, m->id);
        dres_buf_wstr(buf, m->name);
    }       

    return 0;
}


/********************
 * dres_load
 ********************/
EXPORTED dres_t *
dres_load(char *path)
{
    dres_buf_t     buf;
    dres_header_t *hdr = &buf.header;
    dres_t        *dres;
    int            size, status, i;
    

    dres = NULL;
    memset(&buf, 0, sizeof(buf));
    
    if ((buf.fd = open(path, O_RDONLY)) < 0)
        goto fail;

    if (read(buf.fd, hdr, sizeof(*hdr)) != sizeof(*hdr))
        goto fail;
    
#define NTOHL(_f) hdr->_f = ntohl(hdr->_f)
    NTOHL(magic);
    NTOHL(ssize);
    NTOHL(ntarget);
    NTOHL(nprereq);
    NTOHL(ncode);
    NTOHL(sinstr);
    NTOHL(ndependency);
    NTOHL(nvariable);
    NTOHL(ninit);
    NTOHL(nfield);
    NTOHL(nmethod);

    if (hdr->magic != DRES_MAGIC) {
        errno = EINVAL;
        goto fail;
    }

#define SIZE(type, _f) (sizeof(type) * hdr->_f)
    
    size  = SIZE(dres_target_t     , ntarget);
    size += SIZE(dres_prereq_t     , nprereq);
    size += SIZE(vm_chunk_t        , ncode);
    size += SIZE(char              , sinstr);
    size += SIZE(int               , ndependency);
    size += SIZE(dres_variable_t   , nvariable);
    size += SIZE(dres_initializer_t, ninit);
    size += SIZE(dres_init_t       , nfield);
    size += SIZE(vm_method_t       , nmethod);

    buf.dsize = size;
    buf.dused = 0;
    buf.ssize = buf.sused = hdr->ssize;
    
    size += sizeof(*dres) + hdr->ssize;

    if ((dres = (dres_t *)ALLOC_ARR(char, size)) == NULL)
        goto fail;

    if (vm_init(&dres->vm, 0) != 0)
        goto fail;

    buf.strings = ((char *)dres) + sizeof(*dres);
    buf.data    = buf.strings + hdr->ssize;
     
    if (read(buf.fd, buf.strings, hdr->ssize) != (ssize_t)hdr->ssize)
        goto fail;
    
    if ((status = dres_load_targets(dres, &buf)) != 0 ||
        (status = dres_load_factvars(dres, &buf)) != 0 ||
        (status = dres_load_dresvars(dres, &buf)) != 0 ||
        (status = load_initializers(dres, &buf)) != 0 ||
        (status = load_methods(dres, &buf)) != 0) {
        errno = status;
        goto fail;
    }
    
    close(buf.fd);

    if (dres_store_init(dres))
        goto fail;
    if ((status = dres_register_builtins(dres)) != 0) {
        errno = status;
        goto fail;
    }
    

#if 0
    dres_dump_targets(dres);
#endif

    DRES_SET_FLAG(dres, COMPILED);
    DRES_SET_FLAG(dres, ACTIONS_FINALIZED);
    DRES_SET_FLAG(dres, TARGETS_FINALIZED);

    VM_SET_FLAG(&dres->vm, COMPILED);
    
    if (initialize_variables(dres) != 0 || finalize_variables(dres) != 0) {
        errno = EINVAL;
        goto fail;
    }

    dres->vm.nlocal = dres->ndresvar;
    for (i = 0; i < dres->ndresvar; i++)
        vm_set_varname(&dres->vm, i, dres->dresvars[i].name);
    
    return dres;


 fail:
    if (buf.fd >= 0)
        close(buf.fd);
    if (dres)
        FREE(dres);
    
    return NULL;
}


/********************
 * load_initializers
 ********************/
static int
load_initializers(dres_t *dres, dres_buf_t *buf)
{
    dres_initializer_t *init, *previ;
    dres_init_t        *f, *prevf;
    int                 ninit, nfield, i, j;

    ninit = dres_buf_rs32(buf);
    
    for (i = 0, previ = NULL; i < ninit; i++, previ = init) {
        if ((init = dres_buf_alloc(buf, sizeof(*init))) == NULL)
            return ENOMEM;
    
        if (previ == NULL)
            dres->initializers = init;
        else
            previ->next = init;
        
        init->variable = dres_buf_rs32(buf);
        
        nfield = dres_buf_rs32(buf);
        
        for (j = 0, prevf = NULL; j < nfield; j++, prevf = f) {
            if ((f = dres_buf_alloc(buf, sizeof(*f))) == NULL)
                return ENOMEM;

            if (prevf == NULL)
                init->fields = f;
            else
                prevf->next = f;

            f->field.name       = dres_buf_rstr(buf);
            f->field.value.type = dres_buf_rs32(buf);
            
            switch (f->field.value.type) {
            case DRES_TYPE_INTEGER:
            case DRES_TYPE_DRESVAR:
                f->field.value.v.i = dres_buf_rs32(buf);
                break;
            case DRES_TYPE_STRING:
                f->field.value.v.s = dres_buf_rstr(buf);
                break;
            case DRES_TYPE_DOUBLE:
                f->field.value.v.d = dres_buf_rdbl(buf);
                break;
            }
        }
    }

    return 0;
}



/********************
 * load_methods
 ********************/
static int
load_methods(dres_t *dres, dres_buf_t *buf)
{
    vm_method_t *m;
    int          i;

    dres->vm.nmethod = dres_buf_rs32(buf);
    dres->vm.methods =
        dres_buf_alloc(buf, dres->vm.nmethod * sizeof(*dres->vm.methods));
    
    if (dres->vm.methods == NULL)
        return ENOMEM;

    for (i = 0, m = dres->vm.methods; i < dres->vm.nmethod; i++, m++) {
        m->id   = dres_buf_rs32(buf);
        m->name = dres_buf_rstr(buf);
    }       

    return 0;
}




/********************
 * dres_buf_create
 ********************/
dres_buf_t *
dres_buf_create(int dsize, int ssize)
{
    dres_buf_t *buf;

    if (ALLOC_OBJ(buf) == NULL)
        goto fail;

    if ((buf->data    = ALLOC_ARR(char, dsize)) == NULL ||
        (buf->strings = ALLOC_ARR(char, ssize)) == NULL)
        goto fail;
        
    buf->dsize = dsize;
    buf->ssize = ssize;

    buf->strings[0] = '\0';
    buf->strings[1] = '\0';
    buf->sused      = 2;

    return buf;

 fail:
    if (buf) {
        FREE(buf->data);
        FREE(buf->strings);
        FREE(buf);
    }

    return NULL;
}


/********************
 * dres_buf_destroy
 ********************/
void
dres_buf_destroy(dres_buf_t *buf)
{
    if (buf) {
        FREE(buf->data);
        FREE(buf->strings);
        FREE(buf);
    }
}


/********************
 * dres_buf_alloc
 ********************/
void *
dres_buf_alloc(dres_buf_t *buf, size_t size)
{
    char *ptr;
    
    if (buf->error) {
        errno = buf->error;
        return NULL;
    }

    if (buf->dsize - buf->dused < size) {
        ptr = NULL;
        buf->error = errno = ENOMEM;
    }
    else {
        ptr = (void *)buf->data + buf->dused;
        buf->dused += size;
    }

    return ptr;
}


/********************
 * dres_buf_stralloc
 ********************/
char *
dres_buf_stralloc(dres_buf_t *buf, char *str)
{
    char *ptr;
    int   size;
    
    if (buf->error) {
        errno = buf->error;
        return NULL;
    }

    if (str == NULL)
        return buf->strings;

    if (str[0] == '\0')
        return buf->strings + 1;
        
    /* XXX TODO: fold non-empty identical strings as well */
    size = strlen(str) + 1;
    if ((size_t)(buf->ssize - buf->sused) < strlen(str) + 1) {
        buf->error = errno = ENOMEM;
        return NULL;
    }

    ptr         = buf->strings + buf->sused;
    buf->sused += size;

    strcpy(ptr, str);

    return ptr;
}


/********************
 * dres_buf_ws16
 ********************/
int
dres_buf_ws16(dres_buf_t *buf, int16_t i)
{
    int16_t *ptr;

    if ((ptr = dres_buf_alloc(buf, sizeof(int16_t))) == NULL)
        return ENOMEM;

    *ptr = htons(i);
    return 0;
}


/********************
 * dres_buf_wu16
 ********************/
int
dres_buf_wu16(dres_buf_t *buf, u_int16_t i)
{
    u_int16_t *ptr;

    if ((ptr = dres_buf_alloc(buf, sizeof(u_int16_t))) == NULL)
        return ENOMEM;

    *ptr = htons(i);
    return 0;
}


/********************
 * dres_buf_ws32
 ********************/
int
dres_buf_ws32(dres_buf_t *buf, int32_t i)
{
    int32_t *ptr;

    if ((ptr = dres_buf_alloc(buf, sizeof(int32_t))) == NULL)
        return ENOMEM;

    *ptr = htonl(i);
    return 0;
}


/********************
 * dres_buf_wu32
 ********************/
int
dres_buf_wu32(dres_buf_t *buf, u_int32_t i)
{
    u_int32_t *ptr;

    if ((ptr = dres_buf_alloc(buf, sizeof(u_int32_t))) == NULL)
        return ENOMEM;

    *ptr = htonl(i);
    return 0;
}


/********************
 * dres_buf_wstr
 ********************/
int
dres_buf_wstr(dres_buf_t *buf, char *str)
{
    u_int32_t  offs;
    char      *ptr;

    if ((ptr = dres_buf_stralloc(buf, str)) == NULL)
        return ENOMEM;
    
    offs = ptr - buf->strings;
    return dres_buf_wu32(buf, offs);
}


/********************
 * dres_buf_wbuf
 ********************/
int
dres_buf_wbuf(dres_buf_t *buf, char *data, int size)
{
    char *ptr;

    if ((ptr = dres_buf_alloc(buf, size)) == NULL)
        return ENOMEM;

    memcpy(ptr, data, size);
    return 0;
}


/********************
 * dres_buf_wdbl
 ********************/
int
dres_buf_wdbl(dres_buf_t *buf, double d)
{
    int32_t *integer = dres_buf_alloc(buf, sizeof(*integer));
    int32_t *decimal = dres_buf_alloc(buf, sizeof(*decimal));

    /* XXX TODO fixme, this is _not_ the way to do it. */
    DRES_WARNING("%s@%s:%d: FIXME, please...", __FUNCTION__, __FILE__,__LINE__);

    if (integer == NULL || decimal == NULL)
        return ENOMEM;

    *integer = trunc(d);
    if (d < 0.0)
      d = -d;
    d = d - *integer;
    *decimal = (int)(1000 * d);

    *integer = htonl(*integer);
    *decimal = htonl(*decimal);
    
    return 0;
}




/********************
 * dres_buf_rs32
 ********************/
int32_t
dres_buf_rs32(dres_buf_t *buf)
{
    int32_t i;

    if (read(buf->fd, &i, sizeof(i)) != sizeof(i)) {
        i          = 0;
        buf->error = errno;
    }
    
    return ntohl(i);
}


/********************
 * dres_buf_ru32
 ********************/
u_int32_t
dres_buf_ru32(dres_buf_t *buf)
{
    u_int32_t i;

    if (read(buf->fd, &i, sizeof(i)) != sizeof(i)) {
        i          = 0;
        buf->error = errno;
    }
    
    return ntohl(i);
}


/********************
 * dres_buf_rstr
 ********************/
char *
dres_buf_rstr(dres_buf_t *buf)
{
    u_int32_t offs;
    
    if (read(buf->fd, &offs, sizeof(offs)) != sizeof(offs)) {
        buf->error = errno;
        return NULL;
    }

    if ((offs = ntohl(offs)) > buf->ssize) {
        buf->error = EOVERFLOW;
        return NULL;
    }
     
    return buf->strings + offs;
}


/********************
 * dres_buf_rbuf
 ********************/
char *
dres_buf_rbuf(dres_buf_t *buf, int size)
{
    char *ptr;

    if ((ptr = dres_buf_alloc(buf, size)) == NULL) {
        buf->error = ENOMEM;
        return NULL;
    }
    
    if (read(buf->fd, ptr, size) != size) {
        buf->error = errno;
        return NULL;
    }

    return ptr;
}


/********************
 * dres_buf_rdbl
 ********************/
double
dres_buf_rdbl(dres_buf_t *buf)
{
    int32_t integer;
    int32_t decimal;
    double  d;

    /* XXX TODO: fixme, this is _not_ the way to do it */
    if (read(buf->fd, &integer, sizeof(integer)) != sizeof(integer) ||
        read(buf->fd, &decimal, sizeof(decimal)) != sizeof(decimal)) {
        buf->error = errno;
        return 0.0;
    }

    integer = ntohl(integer);
    decimal = ntohl(decimal);
    
    d = 1.0 * integer;
    if (integer < 0)
        d = d - decimal / 1000.0;
    else
        d = d + decimal / 1000.0;
    
    return d;
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
