#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include "mm.h"
#include "vm.h"


/* return a pointer to a newly pused entry */
#define STACK_PUSH(s)                                                   \
    ((s)->nentry < (s)->nalloc ? (s)->entries + (s)->nentry++ : NULL)

/* return a pointer to the top entry of the stack */
#define STACK_TOP(s)                                    \
    ((s)->nentry <= (s)->nalloc && (s)->nentry > 0 ?    \
     (s)->entries + (s)->nentry-1 : NULL)

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
 * vm_type
 ********************/
int
vm_type(vm_stack_t *s)
{
    return STACK_TYPE(s);
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







#ifdef __VM_TEST__


#define STACK_SIZE 50

#define fatal(ec, fmt, args...) do {                            \
        fprintf(stderr, "fatal error: "fmt"\n", ## args);       \
        exit(ec);                                               \
    } while (0)


int
main(int argc, char *argv[])
{
    vm_stack_t *stack = vm_stack_new(1);
    int         i, err, n;
    double      d;
    char       *s;

    if (!stack)
        fatal(ENOMEM, "failed to allocate a stack");
    
    for (i = 0; i < STACK_SIZE; i++) {
    retry:
        switch (i & 0x3) {
        case 0: err = vm_push_int(stack, i);           break;
        case 1: err = vm_push_string(stack, "foobar"); break;
        case 2: err = vm_push_double(stack, 3.0*i);    break;
        case 3: err = vm_push_string(stack, "barfoo"); break;
        }            
        
        if (err == ENOMEM && !vm_stack_grow(stack, 5))
            goto retry;
        else if (err)
            fatal(err, "failed to push integer %d on the stack", i);
        printf("push #%d\n", i);
    }

    for (i = 0; i < STACK_SIZE; i++) {
        switch (vm_type(stack)) {
        case VM_TYPE_INTEGER:
            n = vm_pop_int(stack);
            printf("#%d: popped %d from the stack...\n", i, n);
            break;
            
        case VM_TYPE_DOUBLE:
            d = vm_pop_double(stack);
            printf("#%d: popped %f from the stack...\n", i, d);
            break;

        case VM_TYPE_STRING:
            s = vm_pop_string(stack);
            printf("#%d: popped %s from the stack...\n", i, s);
            break;

        default:
            fatal(EINVAL, "#%d: unexpected object on stack", i);
       }

        vm_stack_trim(stack, 0);

    }

    vm_stack_del(stack);
    
    return 0;
}













#endif /* __VM_TEST__ */







/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
