#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include <dres/mm.h>
#include <dres/vm.h>




int vm_instr_push   (vm_state_t *vm);
int vm_instr_filter (vm_state_t *vm);
int vm_instr_set    (vm_state_t *vm);
int vm_instr_get    (vm_state_t *vm);
int vm_instr_create (vm_state_t *vm);
int vm_instr_call   (vm_state_t *vm);


/*****************************************************************************
 *                            *** code interpreter ***                       *
 *****************************************************************************/


/********************
 * vm_run
 ********************/
int
vm_run(vm_state_t *vm)
{
    int status;

    while (vm->ninstr > 0) {
        switch (VM_OP_CODE(*vm->pc)) {
        case VM_OP_PUSH:   status = vm_instr_push(vm);   break;
        case VM_OP_FILTER: status = vm_instr_filter(vm); break;
        case VM_OP_SET:    status = vm_instr_set(vm);    break;
        case VM_OP_GET:    status = vm_instr_get(vm);    break;
        case VM_OP_CREATE: status = vm_instr_create(vm); break;
        case VM_OP_CALL:   status = vm_instr_call(vm);   break;
        default: VM_EXCEPTION(vm, "invalid instruction 0x%x", *vm->pc);
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
#define CHECK_AND_GROW(t, nbyte) do {                                   \
        if (vm->nsize - sizeof(int) < nbyte)                            \
            VM_EXCEPTION(vm, "push "#t": not enough data");             \
        if (vm_stack_grow(vm->stack, 1))                                \
            VM_EXCEPTION(vm, "push "#t": failed to grow the stack");    \
    } while (0)

    vm_global_t  *g;

    unsigned int  type = VM_PUSH_TYPE(*vm->pc);
    unsigned int  data = VM_PUSH_DATA(*vm->pc);
    char         *name;
    int           nsize;

    
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
            VM_EXCEPTION(vm, "push global: failed to look up %s", name);
        vm_push_global(vm->stack, g);
        nsize = 1 + VM_ALIGN_TO(data, sizeof(int)) / sizeof(int);;
        break;

    default: VM_EXCEPTION(vm, "invalid type 0x%x to push", type);
    }

        
    vm->ninstr--;
    vm->pc    += nsize;
    vm->nsize -= nsize;
    
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
#define FAIL(fmt, args...) do {                 \
        if (g)                                  \
            vm_global_free(g);                  \
        VM_EXCEPTION(vm, fmt, ## args);         \
    } while (0)
    
    vm_global_t *g = NULL;
    int          nfield, nfact;
    char        *field;
    vm_value_t   value;
    int          type, empty;
    int          i, j;
    
    nfield = VM_FILTER_NFIELD(*vm->pc);
    
    if (vm_peek(vm->stack, 2*nfield, &value) != VM_TYPE_GLOBAL)
        FAIL("FILTER: no global found in stack");
    
    g      = value.g;
    nfact  = g->nfact;
    
    for (i = 0; i < nfield; i++) {

        if (vm_type(vm->stack) != VM_TYPE_STRING)
            FAIL("FILTER: invalid field name, string expected");
        
        field = vm_pop_string(vm->stack);
        type  = vm_pop(vm->stack, &value);

        for (j = 0, empty = -1; j < g->nfact; j++) {
            OhmFact    *fact;
            GValue     *gval;
            int         idx;
            
            if ((fact = g->facts[j]) == NULL) {
                if (empty < 0)
                    empty = j;
                continue;
            }

            if (empty >= 0) {
                idx           = empty;
                g->facts[idx] = fact;
                g->facts[j]   = NULL;
                empty         = j;
            }
            else
                idx = j;

            if ((gval = ohm_fact_get(fact, field)) == NULL)
                FAIL("fact has no expected field %s", field);
        
            if (!vm_fact_match_field(vm, fact, field, gval, type, &value)) {
                g_object_unref(fact);
                g->facts[idx] = NULL;
                nfact--;
            }
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
        VM_EXCEPTION(vm, "SET: could not determine fact store");

    dst = vm_pop_global(vm->stack);
    src = vm_pop_global(vm->stack);

    if (src == NULL || dst == NULL) {
        vm_global_free(src);
        vm_global_free(dst);
        VM_EXCEPTION(vm, "SET: could not POP expected two globals");
    }
    
    
    if (VM_GLOBAL_IS_NAME(dst)) {              /* dst a name-only global */
        if (VM_GLOBAL_IS_ORPHAN(src)) {        /* src orphan, assign directly */
            ohm_structure_set_name(OHM_STRUCTURE(src->facts[0]), dst->name);
            if (!ohm_fact_store_insert(store, src->facts[0]))
                VM_EXCEPTION(vm, "SET: failed to insert fact to factstore");
            g_object_unref(src->facts[0]);
            src->facts[0] = NULL;
            src->nfact    = 0;
        }
        else {
            for (i = 0; i < src->nfact; i++) {
                fact = vm_fact_dup(src->facts[i], dst->name);
                if (!ohm_fact_store_insert(store, fact))
                    VM_EXCEPTION(vm, "SET: failed to insert fact to factstore");
            }
        }
    }
    else {
        if (src->nfact != dst->nfact)
            VM_EXCEPTION(vm, "SET: argument dimensions do not match (%d != %d)",
                         src->nfact, dst->nfact);
        
        for (i = 0; i < src->nfact; i++)
            if (vm_fact_copy(dst->facts[i], src->facts[i]) == NULL)
                VM_EXCEPTION(vm, "SET: failed to copy fact");
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
#define FAIL(fmt, args...) do {                 \
        if (g)                                  \
            vm_global_free(g);                  \
        VM_EXCEPTION(vm, fmt, ## args);         \
    } while (0)

    OhmFactStore *store = ohm_fact_store_get_fact_store();
    vm_global_t  *g = NULL;
    char         *field;
    vm_value_t   value;
    int          type;

    if (store == NULL)
        FAIL("SET FIELD: could not determine fact store");

    if (vm_type(vm->stack) != VM_TYPE_STRING)
        FAIL("SET FIELD: invalid field name, string expected");

    field = vm_pop_string(vm->stack);

    if (vm_type(vm->stack) != VM_TYPE_GLOBAL)
        FAIL("SET FIELD: destination, global expected");
    
    g    = vm_pop_global(vm->stack);
    type = vm_pop(vm->stack, &value);
    
    if (g->nfact < 1)
        FAIL("SET FIELD: nonexisting global");

    if (g->nfact > 1)
        FAIL("SET FIELD: cannot set field of multiple globals");
    
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
 * GET (XXX TODO), GET FIELD
 */


/********************
 * vm_instr_get_field
 ********************/
int
vm_instr_get_field(vm_state_t *vm)
{
#define FAIL(fmt, args...) do {                 \
        if (g)                                  \
            vm_global_free(g);                  \
        VM_EXCEPTION(vm, fmt, ## args);         \
    } while (0)

    OhmFactStore *store = ohm_fact_store_get_fact_store();
    vm_global_t  *g = NULL;
    char         *field;
    vm_value_t   value;
    int          type;

    if (store == NULL)
        FAIL("GET FIELD: could not determine fact store");

    if (vm_type(vm->stack) != VM_TYPE_STRING)
        FAIL("GET FIELD: invalid field name, string expected");

    field = vm_pop_string(vm->stack);

    if (vm_type(vm->stack) != VM_TYPE_GLOBAL)
        FAIL("GET FIELD: destination, global expected");
    
    g    = vm_pop_global(vm->stack);
    type = vm_pop(vm->stack, &value);
    
    if (g->nfact < 1)
        FAIL("GET FIELD: nonexisting global");

    if (g->nfact > 1)
        FAIL("GET FIELD: cannot get field of multiple globals");

    type = vm_fact_get_field(vm, g->facts[0], field, &value);
    if (type == VM_TYPE_UNKNOWN)
        FAIL("GET FIELD: global has not field %s", field);

    vm_push(vm->stack, type, value);
    vm_global_free(g);
    
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
    VM_EXCEPTION(vm, "%s not implemented", __FUNCTION__);
    return EOPNOTSUPP; /* not reached */
}


/********************
 * vm_instr_get
 ********************/
int
vm_instr_get(vm_state_t *vm)
{
    if (!VM_OP_ARGS(*vm->pc) & VM_GET_FIELD)
        return vm_instr_get_var(vm);
    else
        return vm_instr_get_field(vm);
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
#define FAIL(fmt, args...) do {                 \
        if (g)                                  \
            vm_global_free(g);                  \
        VM_EXCEPTION(vm, fmt, ## args);         \
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
        FAIL("CREATE: failed to allocate memory for new global");

    if ((fact = ohm_fact_new(VM_UNNAMED_GLOBAL)) == NULL)
        FAIL("CREATE: failed to allocate fact for new global");
    
    for (i = 0; i < nfield; i++) {
        if (vm_type(vm->stack) != VM_TYPE_STRING)
            FAIL("invalid field name, string expected");
        
        field = vm_pop_string(vm->stack);
        type  = vm_pop(vm->stack, &value);

        printf("*** field %s...\n", field);

        if (!vm_fact_set_field(vm, fact, field, type, &value))
            FAIL("failed to add field %s", field);
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
    default: VM_EXCEPTION(vm, "CALL: unknown method ID type 0x%x", type);
    }
    
    if (vm_stack_grow(vm->stack, narg))
        VM_EXCEPTION(vm, "CALL: failed to grow the stack by %d entries", narg);

    status = vm_method_call(vm, name, m, narg);

    if (status)
        VM_EXCEPTION(vm, "CALL: %s failed with error %d", name, status);
    
    vm->ninstr--;
    vm->pc++;
    vm->nsize -= sizeof(int);
    
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

