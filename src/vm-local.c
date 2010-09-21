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
#include <errno.h>

#include <dres/mm.h>
#include <dres/vm.h>

#include "dres-debug.h"


/********************
 * vm_set_varname
 ********************/
int
vm_set_varname(vm_state_t *vm, int id, const char *name)
{
    if (id < 0 || id >= vm->nlocal)
        return EOVERFLOW;
    
    if (vm->names == NULL)
        if ((vm->names = ALLOC_ARR(char *, vm->nlocal)) == NULL)
            return ENOMEM;
    
    if ((vm->names[id] = STRDUP(name)) == NULL)
        return ENOMEM;
    
    return 0;
}


/********************
 * vm_free_varnames
 ********************/
void
vm_free_varnames(vm_state_t *vm)
{
    int i;
    
    if (vm->names != NULL) {
        for (i = 0; i < vm->nlocal; i++) {
            if (vm->names[i] != NULL) {
                FREE(vm->names[i]);
                vm->names[i] = NULL;
            }
        }
        FREE(vm->names);
        vm->names = NULL;
    }
}

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
    case VM_TYPE_NIL: /* happens when setting to the value of an unset local */
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

