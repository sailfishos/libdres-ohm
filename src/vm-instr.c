#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include "mm.h"
#include "vm.h"




int vm_instr_push  (vm_state_t *vm);
int vm_instr_filter(vm_state_t *vm);
int vm_instr_set   (vm_state_t *vm);
int vm_instr_create(vm_state_t *vm);
int vm_instr_call  (vm_state_t *vm);

static int fact_filter_field(vm_state_t *vm, OhmFact *fact, char *field,
                             GValue *gval, int type, vm_value_t *value);
static int fact_set_field   (vm_state_t *vm, OhmFact *fact, char *field,
                             int type, vm_value_t *value);



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
        case VM_OP_CREATE: status = vm_instr_create(vm); break;
        case VM_OP_CALL:   status = vm_instr_call(vm);   break;
        default: VM_EXCEPTION(vm, "invalid instruction 0x%x", *vm->pc);
        }
    }
    
    return status;
}


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
        if (vm_global_lookup(name, &g) < 0)
            VM_EXCEPTION(vm, "push global: failed to look up %s", name);
        vm_push_global(vm->stack, g);
        nsize = 1;
        break;

    default: VM_EXCEPTION(vm, "invalid type 0x%x to push", type);
    }
    
    
    vm->ninstr--;
    vm->pc    += nsize;
    vm->nsize -= nsize;
    
    return 0;
}


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
            FAIL("invalid field name, string expected");
        
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
        
            if (!fact_filter_field(vm, fact, field, gval, type, &value)) {
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
}


/********************
 * fact_filter_field
 ********************/
static int
fact_filter_field(vm_state_t *vm, OhmFact *fact, char *field,
             GValue *gval, int type, vm_value_t *value)
{
    int         i;
    double      d;
    const char *s;

    switch (type) {
    case VM_TYPE_INTEGER:
        switch (G_VALUE_TYPE(gval)) {
        case G_TYPE_INT:   i = g_value_get_int(gval);   break;
        case G_TYPE_UINT:  i = g_value_get_uint(gval);  break;
        case G_TYPE_LONG:  i = g_value_get_long(gval);  break;
        case G_TYPE_ULONG: i = g_value_get_ulong(gval); break;
        default: VM_EXCEPTION(vm, "integer type expected for field %s", field);
        }
        return i == value->i;

    case VM_TYPE_DOUBLE:
        switch (G_VALUE_TYPE(gval)) {
        case G_TYPE_DOUBLE: d = g_value_get_double(gval);    break;
        case G_TYPE_FLOAT:  d = 1.0*g_value_get_float(gval); break;
        default: VM_EXCEPTION(vm, "double type expected for field %s", field);
        }
        return d == value->d;

    case VM_TYPE_STRING:
        switch (G_VALUE_TYPE(gval)) {
        case G_TYPE_STRING: s = g_value_get_string(gval); break;
        default: VM_EXCEPTION(vm, "string type expected for field %s", field);
        }
        return !strcmp(s, value->s);

    default:
        VM_EXCEPTION(vm, "unexpected field type 0x%x for filter", type);
    }
    
    return 0;
}



/********************
 * vm_instr_set
 ********************/
int
vm_instr_set(vm_state_t *vm)
{
    
    return 0;
}


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

    if ((fact = ohm_fact_new("__unnamed_global")) == NULL)
        FAIL("CREATE: failed to allocate fact for new global");

    
    for (i = 0; i < nfield; i++) {
        if (vm_type(vm->stack) != VM_TYPE_STRING)
            FAIL("invalid field name, string expected");
        
        field = vm_pop_string(vm->stack);
        type  = vm_pop(vm->stack, &value);

        printf("*** field %s...\n", field);

        if (!fact_set_field(vm, fact, field, type, &value))
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


/********************
 * fact_set_field
 ********************/
static int
fact_set_field(vm_state_t *vm, OhmFact *fact, char *field,
               int type, vm_value_t *value)
{
    GValue *gval;
    
    switch (type) {
    case VM_TYPE_INTEGER: gval = ohm_value_from_int(value->i);    break;
    case VM_TYPE_DOUBLE:  gval = ohm_value_from_double(value->d); break;
    case VM_TYPE_STRING:  gval = ohm_value_from_string(value->s); break;
    default: VM_EXCEPTION(vm, "invalid type 0x%x for field %s", type, field);
    }

    ohm_fact_set(fact, field, gval);
    return 1;
}


/********************
 * vm_instr_call
 ********************/
int
vm_instr_call(vm_state_t *vm)
{
    return 0;
}


/*****************************************************************************
 *                             *** code generation ***                       *
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

