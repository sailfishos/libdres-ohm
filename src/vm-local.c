#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <dres/mm.h>
#include <dres/vm.h>

#include "dres-debug.h"


/********************
 * vm_scope_push
 ********************/
int
vm_scope_push(vm_state_t *vm)
{
    vm_scope_t *scope = NULL;
    
    if (ALLOC_VAROBJ(scope, vm->nlocal, variables) == NULL)
        return ENOMEM;

    scope->nvariable = vm->nlocal;

    scope->parent = vm->scope;
    vm->scope     = scope;

    return 0;
}


/********************
 * vm_scope_pop
 ********************/
int
vm_scope_pop(vm_state_t *vm)
{
    vm_scope_t *scope;

    if (vm->scope == NULL)
        return ENOENT;
    
    scope     = vm->scope;
    vm->scope = vm->scope->parent;
    
    FREE(scope);
    
    return 0;
}


/********************
 * vm_scope_set
 ********************/
int
vm_scope_set(vm_scope_t *scope, int id, int type, vm_value_t value)
{
    unsigned int idx = VM_LOCAL_INDEX(id);

    if (scope->nvariable <= idx)
        return ENOENT;

    if (scope->variables == NULL)
        return ENOMEM;

    switch (type) {
    case VM_TYPE_INTEGER:
    case VM_TYPE_DOUBLE:
    case VM_TYPE_STRING:
        scope->variables[idx].type = type;
        scope->variables[idx].v    = value;
        return 0;
    default:
        return EINVAL;
    }
}


/********************
 * vm_scope_get
 ********************/
int
vm_scope_get(vm_scope_t *scope, int id, vm_value_t *value)
{
    unsigned int idx = VM_LOCAL_INDEX(id);
    int          type;

    if (scope == NULL || scope->nvariable <= idx || scope->variables == NULL)
        return VM_TYPE_UNKNOWN;
    
    switch ((type = scope->variables[idx].type)) {
    case VM_TYPE_INTEGER:
    case VM_TYPE_DOUBLE:
    case VM_TYPE_STRING:
        *value = scope->variables[idx].v;
        break;

#undef  DISABLE_NESTED_SCOPING
#ifndef DISABLE_NESTED_SCOPING
    case VM_TYPE_UNKNOWN: {
        vm_scope_t *p = scope->parent;
        while (p != NULL && type == VM_TYPE_UNKNOWN) {
            type   = p->variables[idx].type;
            *value = p->variables[idx].v;

            p = p->parent;
        }
    }
        break;
#endif        
        
    default:
        type = VM_TYPE_UNKNOWN;
    }
        
    return type;
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

