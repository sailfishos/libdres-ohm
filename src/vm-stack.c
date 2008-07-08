#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include <dres/mm.h>
#include <dres/vm.h>


/* return a pointer to a newly pushed entry */
#define STACK_PUSH(s)                                                   \
    ((s)->nentry < (s)->nalloc ? (s)->entries + (s)->nentry++ : NULL)

/* return a pointer to the top entry of the stack */
#define STACK_TOP(s)                                    \
    ((s)->nentry <= (s)->nalloc && (s)->nentry > 0 ?    \
     (s)->entries + (s)->nentry-1 : NULL)

#define STACK_ENTRY(s, idx)                                             \
    (((idx) < 0 || (s)->nentry <= (idx)) ? NULL : (STACK_TOP(s) - (idx)))

#define STACK_TYPE(s)                                   \
    ((s)->nentry <= (s)->nalloc && (s)->nentry > 0 ?    \
     (s)->entries[(s)->nentry-1].type : VM_TYPE_UNKNOWN)

#define STACK_POP(s, v)                                         \
    ((s)->nentry <= (s)->nalloc && (s)->nentry > 0 ?            \
     (s)->entries[(s)->nentry--].type : VM_TYPE_UNKNOWN)



/*****************************************************************************
 *                           *** stack management ***                        *
 *****************************************************************************/


/********************
 * vm_stack_new
 ********************/
vm_stack_t *
vm_stack_new(int size)
{
    vm_stack_t *stack;

    if ((stack = ALLOC(vm_stack_t)) == NULL)
        return NULL;

    if (size > 0)
        if ((stack->entries = ALLOC_ARR(vm_stack_entry_t, size)) == NULL) {
            FREE(stack);
            return NULL;
        }
        
    stack->nentry = 0;
    stack->nalloc = size;

    return stack;
}


/********************
 * vm_stack_del
 ********************/
void
vm_stack_del(vm_stack_t *stack)
{
    if (stack) {
        FREE(stack->entries);
        FREE(stack);
    }
}


/********************
 * vm_stack_grow
 ********************/
int
vm_stack_grow(vm_stack_t *s, int nspace)
{
    /* Notes: nspace is the min. desired amount of free stack entries. */

    int nmore = nspace - (s->nalloc - s->nentry);
    
    if (nmore > 0) {
        if (REALLOC_ARR(s->entries, s->nalloc, s->nalloc + nmore) == NULL)
            return ENOMEM;
        s->nalloc += nmore;
    }
    
    return 0;
}


/********************
 * vm_stack_trim
 ********************/
int
vm_stack_trim(vm_stack_t *s, int nspace)
{
    /* Notes: nspace is the max. allowed amount of free stack entries. */

    int nless = (s->nalloc - s->nentry) - nspace;
    int nentry;
    
    if (nless > 0) {
        if ((nentry = s->nalloc - nless) <= 0)
            return 0;             /* avoid freeing via realloc(p, 0) */
        if (REALLOC_ARR(s->entries, s->nalloc, s->nalloc - nless) == NULL)
            return ENOMEM;
        s->nalloc -= nless;
    }
    
    return 0;
}


/********************
 * vm_push_int
 ********************/
int
vm_push_int(vm_stack_t *s, int i)
{
    vm_stack_entry_t *e = STACK_PUSH(s);
    
    if (e == NULL)
        return ENOMEM;
    
    e->type = VM_TYPE_INTEGER;
    e->v.i  = i;
    
    return 0;
}


/********************
 * vm_push_double
 ********************/
int
vm_push_double(vm_stack_t *s, double d)
{
    vm_stack_entry_t *e = STACK_PUSH(s);

    if (e == NULL)
        return ENOMEM;

    e->type = VM_TYPE_DOUBLE;
    e->v.d  = d;

    return 0;
}


/********************
 * vm_push_string
 ********************/
int
vm_push_string(vm_stack_t *s, char *str)
{
    vm_stack_entry_t *e = STACK_PUSH(s);

    if (e == NULL)
        return ENOMEM;

    e->type = VM_TYPE_STRING;
    e->v.s  = str;

    return 0;
}


/********************
 * vm_push_global
 ********************/
int
vm_push_global(vm_stack_t *s, vm_global_t *g)
{
    vm_stack_entry_t *e = STACK_PUSH(s);
    
    if (e == NULL)
        return ENOMEM;
    
    e->type = VM_TYPE_GLOBAL;
    e->v.g  = g;
    
    return 0;
}


/********************
 * vm_push
 ********************/
int
vm_push(vm_stack_t *s, int type, vm_value_t value)
{
    vm_stack_entry_t *e = STACK_PUSH(s);
    
    if (e == NULL)
        return ENOMEM;
    
    e->type = type;
    e->v    = value;

    return 0;
}


/********************
 * vm_type
 ********************/
int
vm_type(vm_stack_t *s)
{
    return STACK_TYPE(s);
}


/********************
 * vm_peek
 ********************/
int
vm_peek(vm_stack_t *s, int idx, vm_value_t *value)
{
    vm_stack_entry_t *e = STACK_ENTRY(s, idx);
    int               t;

    if (e == NULL)
        return VM_TYPE_UNKNOWN;
    
    *value = e->v;
    t      = e->type;
    
    return t;
}


/********************
 * vm_args
 ********************/
vm_stack_entry_t *
vm_args(vm_stack_t *s, int narg)
{
    return STACK_ENTRY(s, narg - 1);
}


/********************
 * vm_pop
 ********************/
int
vm_pop(vm_stack_t *s, vm_value_t *value)
{
    vm_stack_entry_t *e = STACK_TOP(s);
    int               t;

    if (e == NULL)
        return VM_TYPE_UNKNOWN;
    
    *value = e->v;
    t      = e->type;
    
    e->type = VM_TYPE_UNKNOWN;
    s->nentry--;
    
    return t;
}


/********************
 * vm_pop_int
 ********************/
int
vm_pop_int(vm_stack_t *s)
{
    vm_stack_entry_t *e = STACK_TOP(s);

    if (e == NULL || e->type != VM_TYPE_INTEGER)
        return INT_MAX;
    
    e->type = VM_TYPE_UNKNOWN;
    s->nentry--;

    return e->v.i;
}


/********************
 * vm_pop_double
 ********************/
double
vm_pop_double(vm_stack_t *s)
{
    vm_stack_entry_t *e = STACK_TOP(s);

    if (e == NULL || e->type != VM_TYPE_DOUBLE)
        return 666.666;
    
    e->type = VM_TYPE_UNKNOWN;
    s->nentry--;

    return e->v.d;
}


/********************
 * vm_pop_string
 ********************/
char *
vm_pop_string(vm_stack_t *s)
{
    vm_stack_entry_t *e = STACK_TOP(s);

    if (e == NULL || e->type != VM_TYPE_STRING)
        return NULL;
    
    e->type = VM_TYPE_UNKNOWN;
    s->nentry--;

    return e->v.s;
}


/********************
 * vm_pop_global
 ********************/
vm_global_t *
vm_pop_global(vm_stack_t *s)
{
    vm_stack_entry_t *e = STACK_TOP(s);

    if (e == NULL || e->type != VM_TYPE_GLOBAL)
        return NULL;
    
    e->type = VM_TYPE_UNKNOWN;
    s->nentry--;

    return e->v.g;
}







/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
