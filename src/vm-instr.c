#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include <dres/mm.h>
#include <dres/vm.h>

#include "dres-debug.h"


int vm_instr_push   (vm_state_t *vm);
int vm_instr_pop    (vm_state_t *vm);
int vm_instr_filter (vm_state_t *vm);
int vm_instr_update (vm_state_t *vm);
int vm_instr_set    (vm_state_t *vm);
int vm_instr_get    (vm_state_t *vm);
int vm_instr_create (vm_state_t *vm);
int vm_instr_call   (vm_state_t *vm);
int vm_instr_cmp    (vm_state_t *vm);
int vm_instr_branch (vm_state_t *vm);
int vm_instr_debug  (vm_state_t *vm);


/*****************************************************************************
 *                            *** code interpreter ***                       *
 *****************************************************************************/


/********************
 * vm_run
 ********************/
int
vm_run(vm_state_t *vm)
{
    int status = EOPNOTSUPP;

    while (vm->ninstr > 0) {
        if (DEBUG_ON(DBG_VM)) {
            unsigned int *pc = vm->pc;
            char          instr[128];
            int           n;
            
            if ((n = vm_dump_instr(&pc, instr, sizeof(instr), 0)) > 0) {
                if (instr[n-1] == '\n')
                    instr[n-1] = '\0';
                DEBUG(DBG_VM, "executing %s", instr);
            }
        }
        
        switch ((vm_opcode_t)VM_OP_CODE(*vm->pc)) {
        case VM_OP_PUSH:   status = vm_instr_push(vm);   break;
        case VM_OP_POP:    status = vm_instr_pop(vm);    break;
        case VM_OP_FILTER: status = vm_instr_filter(vm); break;
        case VM_OP_UPDATE: status = vm_instr_update(vm); break;
        case VM_OP_SET:    status = vm_instr_set(vm);    break;
        case VM_OP_GET:    status = vm_instr_get(vm);    break;
        case VM_OP_CREATE: status = vm_instr_create(vm); break;
        case VM_OP_CALL:   status = vm_instr_call(vm);   break;
        case VM_OP_CMP:    status = vm_instr_cmp(vm);    break;
        case VM_OP_BRANCH: status = vm_instr_branch(vm); break;
        case VM_OP_DEBUG:  status = vm_instr_debug(vm);  break;
        case VM_OP_HALT:   return status;
        default: VM_RAISE(vm, EILSEQ, "invalid instruction 0x%x", *vm->pc);
        }
    }
    
    return status;
}



/*
 * PUSH
 */


/********************
 * vm_instr_push
 ********************/
int
vm_instr_push(vm_state_t *vm)
{
#define CHECK_AND_GROW(t, nbyte) do {                                    \
        if (vm->nsize - sizeof(int) < nbyte)                             \
            VM_RAISE(vm, EINVAL, "PUSH "#t": not enough data");          \
        if (vm_stack_grow(vm->stack, nbyte / sizeof(int)))               \
            VM_RAISE(vm, ENOMEM, "PUSH "#t": failed to grow the stack"); \
    } while (0)

    vm_global_t  *g;
    vm_value_t    v;

    unsigned int  type = VM_PUSH_TYPE(*vm->pc);
    unsigned int  data = VM_PUSH_DATA(*vm->pc);
    unsigned int  i;
    char         *name;
    int           nsize, id;
    

    switch (type) {
    case VM_TYPE_INTEGER:
        CHECK_AND_GROW(int, sizeof(int));
        vm_push_int(vm->stack, data ? data - 1 : *(vm->pc + 1));
        nsize = data ? 1 : 2;
        break;

    case VM_TYPE_DOUBLE:
        CHECK_AND_GROW(double, sizeof(double));
        vm_push_double(vm->stack, *(double *)(vm->pc + 1));
        nsize = 1 + sizeof(double) / sizeof(int);
        break;

    case VM_TYPE_STRING:
        CHECK_AND_GROW(char *, data);
        vm_push_string(vm->stack, (char *)(vm->pc + 1));
        nsize = 1 + VM_ALIGN_TO(data, sizeof(int)) / sizeof(int);
        break;

    case VM_TYPE_GLOBAL:
        CHECK_AND_GROW(char *, data);
        name = (char *)(vm->pc + 1);
        if (vm_global_lookup(name, &g) == ENOENT)
            g = vm_global_name(name);
        if (g == NULL)
            VM_RAISE(vm, ENOENT, "PUSH GLOBAL: failed to look up %s", name);
        vm_push_global(vm->stack, g);
        nsize = 1 + VM_ALIGN_TO(data, sizeof(int)) / sizeof(int);;
        break;

    case VM_TYPE_LOCAL:
        if (vm_scope_push(vm) != 0)
            VM_RAISE(vm, ENOMEM, "PUSH LOCALS: failed to push new scope");
        for (i = 0; i < data; i++) {
            if (vm_type(vm->stack) != VM_TYPE_INTEGER)
                VM_RAISE(vm, EINVAL, "PUSH LOCALS: expecting integer ID");
            id   = vm_pop_int(vm->stack);
            type = vm_pop(vm->stack, &v);
            if (vm_scope_set(vm->scope, id, type, v) != 0)
                VM_RAISE(vm, EINVAL,
                             "PUSH LOCALS: failed to set local #0x%x", id);
        }
        nsize = 1;
        break;
        
    default: VM_RAISE(vm, EINVAL, "invalid type 0x%x to push", type);
    }

        
    vm->ninstr--;
    vm->pc    += nsize;
    vm->nsize -= nsize * sizeof(int);
    
    return 0;
}


/*
 * POP
 */

/********************
 * vm_instr_pop
 ********************/
int
vm_instr_pop(vm_state_t *vm)
{
    int        kind = VM_OP_ARGS(*vm->pc);
    int        type;
    vm_value_t value;
    
    
    switch (kind) {
    case VM_POP_LOCALS:
        if (vm_scope_pop(vm) != 0)
            VM_RAISE(vm, EINVAL, "POP LOCALS: failed to pop scope");
        break;
        
    case VM_POP_DISCARD:
        if ((type = vm_pop(vm->stack, &value)) == VM_TYPE_GLOBAL)
            vm_global_free(value.g);
        break;

    default:
        VM_RAISE(vm, EINVAL, "POP: invalid POP type 0x%x", kind);
    }
    
    vm->ninstr--;
    vm->pc    += 1;
    vm->nsize -= sizeof(int);

    return 0;
}


/*
 * FILTER
 */


/********************
 * vm_instr_filter
 ********************/
int
vm_instr_filter(vm_state_t *vm)
{
#define FAIL(err, fmt, args...) do {       \
        if (g)                             \
            vm_global_free(g);             \
        VM_RAISE(vm, err, fmt, ## args);   \
    } while (0)
    
    vm_global_t *g = NULL;
    int          nfield, nfact;
    char        *field;
    vm_value_t   value;
    int          type;
    int          i, j;
    
    nfield = VM_FILTER_NFIELD(*vm->pc);
    
    if (vm_peek(vm->stack, 2*nfield, &value) != VM_TYPE_GLOBAL)
        FAIL(ENOENT, "FILTER: no global found in stack");
    
    g      = value.g;
    nfact  = g->nfact;
    
    for (i = 0; i < nfield; i++) {

        if (vm_type(vm->stack) != VM_TYPE_STRING)
            FAIL(EINVAL, "FILTER: invalid field name, string expected");
        
        field = vm_pop_string(vm->stack);
        type  = vm_pop(vm->stack, &value);

        for (j = 0; j < g->nfact; j++) {
            OhmFact    *fact;
            GValue     *gval;
            
            if ((fact = g->facts[j]) == NULL)
                continue;

            if ((gval = ohm_fact_get(fact, field)) == NULL)
                FAIL(ENOENT, "fact has no expected field %s", field);
        
            if (!vm_fact_match_field(vm, fact, field, gval, type, &value)) {
                g_object_unref(fact);
                g->facts[j] = NULL;
                nfact--;
            }
        }
    }

    if (nfact != g->nfact) {
        for (i = 0, j = 0; j < nfact; i++) {       /* pack facts tightly */
            if (g->facts[i] != NULL)
                g->facts[j++] = g->facts[i];
        }
    }
    
    g->nfact = nfact;
    

    vm->ninstr--;
    vm->pc++;
    vm->nsize -= sizeof(int);

    return 0;
#undef FAIL
}


/*
 * UPDATE
 */


/********************
 * vm_instr_update
 ********************/
int
vm_instr_update(vm_state_t *vm)
{
#define FAIL(err, fmt, args...) do {                                    \
        if (src) vm_global_free(src);                                   \
        if (dst) vm_global_free(dst);                                   \
        VM_RAISE(vm, err, fmt, ## args);                                \
    } while (0)

    vm_global_t *src, *dst;
    int          nsrc, ndst;
    vm_value_t   sval, dval;
    OhmFact     *sfact, *dfact;
    int          partial, nfield, i, j, success;
    int          match;
    
    src     = NULL;
    dst     = NULL;
    nfield  = VM_UPDATE_NFIELD(*vm->pc);
    partial = VM_UPDATE_PARTIAL(*vm->pc);

    {
        char   *fields[nfield];
        GValue *values[nfield];
        
        if (vm_peek(vm->stack, nfield, &dval) != VM_TYPE_GLOBAL)
            FAIL(ENOENT, "UPDATE: no global destination found in stack");
    
        if (vm_peek(vm->stack, nfield + 1, &sval) != VM_TYPE_GLOBAL)
            FAIL(ENOENT, "UPDATE: no global source found in stack");
    
        for (i = 0; i < nfield; i++)
            if ((fields[i] = vm_pop_string(vm->stack)) == NULL)
                FAIL(ENOENT, "UPDATE: expected #%d field name not in stack", i);
    
        dst  = dval.g;
        ndst = dst->nfact;
        src  = sval.g;
        nsrc = src->nfact;
        
        vm_pop_global(vm->stack);                    /* pop destination */
        vm_pop_global(vm->stack);                    /* pop source */
        
        for (i = 0; i < nsrc; i++) {
            sfact = src->facts[i];
            if ((j = vm_fact_collect_fields(sfact, fields, nfield, values)) < 0)
                FAIL(ENOENT, "UPDATE: source has no field %s", fields[-j]);
            
            match = FALSE;
            for (j = vm_global_find_first(dst, fields, values, nfield);
                 j >= 0;
                 j = vm_global_find_next(dst, j, fields, values, nfield)) {
                match = TRUE;
                
                dfact = dst->facts[j];
                if (partial)
                    success = (vm_fact_update(dfact, sfact) != NULL);
                else
                    success = (vm_fact_copy(dfact, sfact) != NULL);
                if (!success)
                    FAIL(EINVAL, "UPDATE: failed to update source fact #%d", i);
            }
            
            if (!match)
                FAIL(ENOENT,
                     "UPDATE: source #%d has no matching destination", i);
            
            g_object_unref(sfact);
            src->facts[i] = NULL;
            src->nfact--;
        }
        for (j = 0; j < dst->nfact; j++) {
            g_object_unref(dst->facts[j]);
            dst->facts[j] = NULL;
            dst->nfact--;
        }
    }
    
    if (src)
        vm_global_free(src);
    if (dst)
        vm_global_free(dst);

    vm->ninstr--;
    vm->pc++;
    vm->nsize -= sizeof(int);

    return 0;
#undef FAIL
}


/*
 * SET, SET FIELD
 */


/********************
 * vm_instr_set_var
 ********************/
int
vm_instr_set_var(vm_state_t *vm)
{
    OhmFactStore *store = ohm_fact_store_get_fact_store();
    OhmFact      *fact;
    vm_global_t  *src, *dst;
    int          i;

    if (store == NULL)
        VM_RAISE(vm, EINVAL, "SET: could not determine fact store");

    dst = vm_pop_global(vm->stack);
    src = vm_pop_global(vm->stack);

    if (src == NULL || dst == NULL) {
        vm_global_free(src);
        vm_global_free(dst);
        VM_RAISE(vm, ENOENT, "SET: could not POP expected two globals");
    }
    
    
    if (VM_GLOBAL_IS_NAME(dst)) {              /* dst a name-only global */
        if (VM_GLOBAL_IS_ORPHAN(src)) {        /* src orphan, assign directly */
            ohm_structure_set_name(OHM_STRUCTURE(src->facts[0]), dst->name);
            if (!ohm_fact_store_insert(store, src->facts[0]))
                VM_RAISE(vm, ENOMEM, "SET: failed to insert fact to factstore");
            g_object_unref(src->facts[0]);
            src->facts[0] = NULL;
            src->nfact    = 0;
        }
        else {
            for (i = 0; i < src->nfact; i++) {
                fact = vm_fact_dup(src->facts[i], dst->name);
                if (!ohm_fact_store_insert(store, fact))
                    VM_RAISE(vm, ENOMEM,
                             "SET: failed to insert fact to factstore");
            }
        }
    }
    else {
        if (src->nfact != dst->nfact)
            VM_RAISE(vm, EINVAL,
                         "SET: argument dimensions do not match (%d != %d)",
                         src->nfact, dst->nfact);
        
        for (i = 0; i < src->nfact; i++)
            if (vm_fact_copy(dst->facts[i], src->facts[i]) == NULL)
                VM_RAISE(vm, EINVAL, "SET: failed to copy fact");
    }
    
    vm_global_free(src);
    vm_global_free(dst);
    
    vm->ninstr--;
    vm->pc++;
    vm->nsize -= sizeof(int);
    
    return 0;
}


/********************
 * vm_instr_set_field
 ********************/
int
vm_instr_set_field(vm_state_t *vm)
{
#define FAIL(err, fmt, args...) do {            \
        if (g)                                  \
            vm_global_free(g);                  \
        VM_RAISE(vm, err, fmt, ## args);    \
    } while (0)

    OhmFactStore *store = ohm_fact_store_get_fact_store();
    vm_global_t  *g = NULL;
    char         *field;
    vm_value_t   value;
    int          type;

    if (store == NULL)
        FAIL(EINVAL, "SET FIELD: could not determine fact store");

    if (vm_type(vm->stack) != VM_TYPE_STRING)
        FAIL(EINVAL, "SET FIELD: invalid field name, string expected");

    field = vm_pop_string(vm->stack);

    if (vm_type(vm->stack) != VM_TYPE_GLOBAL)
        FAIL(EINVAL, "SET FIELD: destination, global expected");
    
    g    = vm_pop_global(vm->stack);
    type = vm_pop(vm->stack, &value);
    
    if (g->nfact < 1)
        FAIL(ENOENT, "SET FIELD: nonexisting global");

    if (g->nfact > 1)
        FAIL(EINVAL, "SET FIELD: cannot set field of multiple globals");
    
    vm_fact_set_field(vm, g->facts[0], field, type, &value);
    vm_global_free(g);
    
    vm->ninstr--;
    vm->pc++;
    vm->nsize -= sizeof(int);
    
    return 0;
}


/********************
 * vm_instr_set
 ********************/
int
vm_instr_set(vm_state_t *vm)
{
    if (!VM_OP_ARGS(*vm->pc) & VM_SET_FIELD)
        return vm_instr_set_var(vm);
    else
        return vm_instr_set_field(vm);
}


/*
 * GET, GET FIELD
 */


/********************
 * vm_instr_get_field
 ********************/
int
vm_instr_get_field(vm_state_t *vm)
{
#define FAIL(err, fmt, args...) do {            \
        if (g)                                  \
            vm_global_free(g);                  \
        VM_RAISE(vm, err, fmt, ## args);    \
    } while (0)

    OhmFactStore *store = ohm_fact_store_get_fact_store();
    vm_global_t  *g = NULL;
    char         *field;
    vm_value_t   value;
    int          type;

    if (store == NULL)
        FAIL(EINVAL, "GET FIELD: could not determine fact store");

    if (vm_type(vm->stack) != VM_TYPE_STRING)
        FAIL(EINVAL, "GET FIELD: invalid field name, string expected");

    field = vm_pop_string(vm->stack);

    if (vm_type(vm->stack) != VM_TYPE_GLOBAL)
        FAIL(EINVAL, "GET FIELD: destination, global expected");
    
    g = vm_pop_global(vm->stack);
    
    if (g->nfact < 1)
        FAIL(ENOENT, "GET FIELD: nonexisting global");

    if (g->nfact > 1)
        FAIL(EINVAL, "GET FIELD: cannot get field of multiple globals");

    type = vm_fact_get_field(vm, g->facts[0], field, &value);
    if (type == VM_TYPE_UNKNOWN)
        FAIL(ENOENT, "GET FIELD: global has no field %s", field);

    vm_push(vm->stack, type, value);
    vm_global_free(g);
    
    vm->ninstr--;
    vm->pc++;
    vm->nsize -= sizeof(int);
    
    return 0;
}


/********************
 * vm_instr_get_local
 ********************/
int
vm_instr_get_local(vm_state_t *vm)
{
    vm_value_t value;
    int        type, err;
    int        idx = VM_OP_ARGS(*vm->pc) & ~VM_GET_LOCAL;
    
    if ((type = vm_scope_get(vm->scope, idx, &value)) == VM_TYPE_UNKNOWN) {
        type    = VM_TYPE_NIL;
        value.i = 0;
    }
    
    if ((err = vm_push(vm->stack, type, value)) != 0)
        VM_RAISE(vm, ENOMEM,
                     "GET LOCAL: failed to push value of #0x%x", idx);

    vm->ninstr--;
    vm->pc++;
    vm->nsize -= sizeof(int);
    
    return 0;
}



/********************
 * vm_instr_get_var
 ********************/
int
vm_instr_get_var(vm_state_t *vm)
{
    /*
     * XXX TODO implement me: fetch named local variable and push its
     *                        value on the stack
     */

    /*
     * XXX TODO What ??? Isn't vm_instr_get_local doing exactly that ???
     *          Hmm... What the heck was I thinking of here ?
     */

    VM_RAISE(vm, ENOSYS, "%s not implemented", __FUNCTION__);
    return EOPNOTSUPP; /* not reached */

    (void)vm;
}



/********************
 * vm_instr_get
 ********************/
int
vm_instr_get(vm_state_t *vm)
{
    if (VM_OP_ARGS(*vm->pc) & VM_GET_FIELD)
        return vm_instr_get_field(vm);
    else if (VM_OP_ARGS(*vm->pc) & VM_GET_LOCAL)
        return vm_instr_get_local(vm);
    else
        return vm_instr_get_var(vm);
}


/*
 * CREATE
 */


/********************
 * vm_instr_create
 ********************/
int
vm_instr_create(vm_state_t *vm)
{
#define FAIL(err, fmt, args...) do {            \
        if (g)                                  \
            vm_global_free(g);                  \
        VM_RAISE(vm, err, fmt, ## args);    \
    } while (0)
    
    vm_global_t *g = NULL;
    OhmFact     *fact;
    int          nfield;
    char        *field;
    vm_value_t   value;
    int          type;
    int          i;
    
    nfield = VM_CREATE_NFIELD(*vm->pc);    

    printf("*** %d fields to create...\n", nfield);
    
    if (ALLOC_VAROBJ(g, nfield, facts) == NULL)
        FAIL(ENOMEM, "CREATE: failed to allocate memory for new global");

    if ((fact = ohm_fact_new(VM_UNNAMED_GLOBAL)) == NULL)
        FAIL(ENOMEM, "CREATE: failed to allocate fact for new global");
    
    for (i = 0; i < nfield; i++) {
        if (vm_type(vm->stack) != VM_TYPE_STRING)
            FAIL(EINVAL, "invalid field name, string expected");
        
        field = vm_pop_string(vm->stack);
        type  = vm_pop(vm->stack, &value);

        printf("*** field %s...\n", field);

        if (!vm_fact_set_field(vm, fact, field, type, &value))
            FAIL(ENOMEM, "failed to add field %s", field);
    }

    g->facts[0] = fact;
    g->nfact    = 1;
    vm_push_global(vm->stack, g);
    
    vm->ninstr--;
    vm->pc++;
    vm->nsize -= sizeof(int);

    return 0;
}


/*
 * CALL
 */


/********************
 * vm_instr_call
 ********************/
int
vm_instr_call(vm_state_t *vm)
{
    vm_value_t   id;
    vm_method_t *m;
    int          narg = VM_OP_ARGS(*vm->pc);
    int          type, status;
    char        *name;

    switch ((type = vm_pop(vm->stack, &id))) {
    case VM_TYPE_STRING:  m = vm_method_lookup(vm, id.s); name = id.s; break;
    case VM_TYPE_INTEGER: m = vm_method_by_id(vm, id.i);  name = m->name; break;
    default: VM_RAISE(vm, EINVAL, "CALL: unknown method ID type 0x%x",type);
    }
    
    if (vm_stack_grow(vm->stack, narg))
        VM_RAISE(vm, ENOMEM,
                     "CALL: failed to grow the stack by %d entries", narg);

    status = vm_method_call(vm, name, m, narg);

    if (status < 0)
        VM_RAISE(vm, status,
                 "CALL: method '%s' failed (error %d)", name, status);
    else if (status == 0)
        VM_FAIL(vm, "CALL: method '%s' failed without an error", name);
    
    vm->ninstr--;
    vm->pc++;
    vm->nsize -= sizeof(int);
    
    return 0;
}


/*
 * CMP
 */

/********************
 * vm_instr_cmp
 ********************/
int
vm_instr_cmp(vm_state_t *vm)
{
    vm_relop_t op;
    vm_value_t arg1, arg2;
    int        type1, type2, result;
    
    op = VM_CMP_RELOP(*vm->pc);

    type1 = vm_pop(vm->stack, &arg1);
    if (type1 == VM_TYPE_UNKNOWN)
        VM_RAISE(vm, ENOENT, "CMP: could not POP expected argument #1");
    
    if (op != VM_RELOP_NOT) {
        type2 = vm_pop(vm->stack, &arg2);
        if (type2 == VM_TYPE_UNKNOWN)
            VM_RAISE(vm, ENOENT, "CMP: could not POP expected argument #2");

        if (type1 != type2)
            VM_RAISE(vm, EINVAL, "CMP: arguments with different types");
    }
    
    
#define COMPARE(v1, cmp_op, v2) do {                                       \
        switch ((vm_type_t)type1) {                                        \
        case VM_TYPE_INTEGER: result = (v1.i cmp_op v2.i);          break; \
        case VM_TYPE_DOUBLE:  result = (v1.d cmp_op v2.d);          break; \
        case VM_TYPE_STRING:  result = strcmp(v1.s, v2.s) cmp_op 0; break; \
        default: VM_RAISE(vm, EINVAL, "CMP: invalid type 0x%x", type1);    \
        }                                                                  \
    } while (0)
    

    if (op != VM_RELOP_NOT) {
        switch (op) {
        case VM_RELOP_EQ: COMPARE(arg1, ==, arg2); break;
        case VM_RELOP_NE: COMPARE(arg1, !=, arg2); break;
        case VM_RELOP_LT: COMPARE(arg1, < , arg2); break;
        case VM_RELOP_LE: COMPARE(arg1, <=, arg2); break;
        case VM_RELOP_GT: COMPARE(arg1, > , arg2); break;
        case VM_RELOP_GE: COMPARE(arg1, >=, arg2); break;
        default: VM_RAISE(vm, EINVAL, "CMP: invalid type 0x%x", type1);
        }
    }
    else {
        switch ((vm_type_t)type1) {
        case VM_TYPE_INTEGER: result = (arg1.i != 0   ); break;
        case VM_TYPE_DOUBLE:  result = (arg1.d == 0.0 ); break;
        case VM_TYPE_STRING:  result = (arg1.s != NULL); break;
        default: VM_RAISE(vm, EINVAL, "CMP: invalid type 0x%x", type1);
        }
    }
    
#undef COMPARE
    
    vm_push_int(vm->stack, result);
    
    vm->ninstr--;
    vm->pc++;
    vm->nsize -= sizeof(int);

    return 0;
}


/*
 * BRANCH
 */

/********************
 * vm_instr_branch
 ********************/
int
vm_instr_branch (vm_state_t *vm)
{
    int        brtype, brdiff, branch;
    int        type;
    vm_value_t value;

    brtype = VM_BRANCH_TYPE(*vm->pc);
    brdiff = VM_BRANCH_DIFF(*vm->pc);

    switch ((vm_branch_t)brtype) {
    case VM_BRANCH:
        branch = TRUE;
        break;
    case VM_BRANCH_EQ:
    case VM_BRANCH_NE:
        type = vm_pop(vm->stack, &value);
        if (type == VM_TYPE_UNKNOWN)
            VM_RAISE(vm, ENOENT, "BRANCH: could not pop expected argument");
        
        switch ((vm_type_t)type) {
        case VM_TYPE_INTEGER: branch = (value.i != 0       ); break;
        case VM_TYPE_DOUBLE:  branch = (value.d != 0.0     ); break;
        case VM_TYPE_STRING:  branch = (value.s && *value.s); break;
        case VM_TYPE_GLOBAL:  branch = (value.g->nfact > 0 ); break;
        default:
            VM_RAISE(vm, EINVAL, "BRANCH: argument of invalid type 0x%x", type);
        }
        
        if (brtype == VM_BRANCH_NE)
            branch = !branch;
        break;
        
    default:
        VM_RAISE(vm, EINVAL, "BRANCH: invalid branch type 0x%x", brtype);
    }


    /*
     * Notes:
     *   In hindsight, bookeeping of ninstr and nize and using ninstr > 0 as
     *   the terminating condition for running the VM was arguably a bad idea.
     *   It was originally designed for an assignment-only language with
     *   strictly linear execution.
     *
     *   With the introduction of branching it all falls nicely apart as there
     *   is no reasonable (= O(1)) way of determining the new values for
     *   ninstr and nsize when we do branch. Of course we could go into the
     *   trouble of calculating both ninstr and nsize diffs during compilation
     *   encode then into the instruction itself but it would be rather
     *   pointless.
     *
     *   The halt instruction was introduced to solve this problem: simply run
     *   the VM until executing a halt instruction. However, the rest of the
     *   VM execution still updates ninstr and nsize. The main driving loop
     *   still uses ninstr (incorrectly; consider branches backwards with
     *   negative diffs) in addition to the halt instruction. It needs to be
     *   corrected to only use halt.
     */
    
#if 0
    vm->ninstr--;
    vm->nsize -= sizeof(int);
    vm->pc++;
#endif
    
    if (branch) {
        if (brdiff > 0) {
            if (vm->nsize < (int)(brdiff * sizeof(int)))
                VM_RAISE(vm, EOVERFLOW, "branch beyond end of code");
        }
        else {
            if ((int)(brdiff * sizeof(int)) > vm->nsize)
                VM_RAISE(vm, EOVERFLOW, "branch beyond beginning of code");
        }
        
        vm->pc += brdiff;
    }
    else
        vm->pc++;
    
    return 0;
}


/*
 * DEBUG
 */


/********************
 * vm_instr_debug
 ********************/
int
vm_instr_debug(vm_state_t *vm)
{
    char *info  = (char *)(vm->pc + 1);
    int   len   = VM_DEBUG_LEN(*vm->pc);
    int   nsize = 1 + VM_ALIGN_TO(len, sizeof(int)) / sizeof(int);
    
    DEBUG(DBG_VM, "%s", info);
    vm->info = info;

    vm->ninstr--;
    vm->pc    += nsize;
    vm->nsize -= nsize * sizeof(int);
    
    return 0;
}

/*****************************************************************************
 *                        *** (code) chunk generation ***                    *
 *****************************************************************************/

/********************
 * vm_chunk_new
 ********************/
vm_chunk_t *
vm_chunk_new(int ninstr)
{
    vm_chunk_t *chunk;

    if ((chunk = ALLOC(vm_chunk_t)) == NULL)
        return NULL;

    if (ninstr > 0)
        if ((chunk->instrs = (unsigned *)ALLOC_ARR(int, ninstr)) == NULL) {
            FREE(chunk);
            return NULL;
        }

    chunk->ninstr = 0;
    chunk->nsize  = 0;
    chunk->nleft  = ninstr * sizeof(int);

    return chunk;
}


/********************
 * vm_chunk_del
 ********************/
void
vm_chunk_del(vm_chunk_t *chunk)
{
    if (chunk) {
        FREE(chunk->instrs);
        FREE(chunk);
    }
}


/********************
 * vm_chunk_grow
 ********************/
unsigned int *
vm_chunk_grow(vm_chunk_t *c, int nsize)
{
    /* Notes: nsize is the min. desired amount of free space in the buffer. */
    
    if ((nsize = nsize - c->nleft) > 0) {
        int nold = c->nsize + c->nleft;
        if (REALLOC_ARR(c->instrs, nold, nold + nsize) == NULL)
            return NULL;
        c->nleft += nsize;
    }
    
    return (unsigned int *)(((char *)c->instrs) + c->nsize);
}


/********************
 * vm_chunk_add
 ********************/
int
vm_chunk_add(vm_chunk_t *c, unsigned int *code, int ninstr, int nsize)
{
    unsigned int *cp = vm_chunk_grow(c, nsize);

    if (cp == NULL)
        return ENOMEM;

    memcpy(cp, code, nsize);
    c->ninstr += ninstr;
    c->nsize  += nsize;
    c->nleft  -= nsize;

    return 0;
}




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

