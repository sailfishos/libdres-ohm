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
#include <errno.h>

#include <dres/mm.h>
#include <dres/vm.h>

#define UNKNOWN_ID 0xefffffff

static int vm_unknown_handler(void *data, char *name,
                              vm_stack_entry_t *args, int narg,
                              vm_stack_entry_t *retval);

static vm_method_t default_method = {
 name:    "default",
 id:      UNKNOWN_ID,
 handler: vm_unknown_handler,
 data:    NULL
};


/********************
 * vm_method_add
 ********************/
int
vm_method_add(vm_state_t *vm, char *name, vm_action_t handler, void *data)
{
    vm_method_t *m;
    
    if ((m = vm_method_lookup(vm, name)) != &default_method) {
        if (m->handler != NULL)
            return EEXIST;
    }
    else {
        if (REALLOC_ARR(vm->methods, vm->nmethod, vm->nmethod + 1) == NULL)
            return ENOMEM;

        m = vm->methods + vm->nmethod;
        
        m->name = STRDUP(name);
        m->id   = vm->nmethod;

        if (m->name == NULL)
            return ENOMEM;
        
        vm->nmethod++;
    }

    if (handler != NULL) {
        m->handler = handler;
        m->data    = data;
    }
    
    return 0;
}


/********************
 * vm_method_del
 ********************/
int
vm_method_del(vm_state_t *vm, char *name, vm_action_t handler)
{
    vm_method_t *m;
    
    if ((m = vm_method_lookup(vm, name)) != &default_method)
        return ENOENT;
    
    if (m->handler != handler)
        return EINVAL;
    
    m->handler = NULL;
    m->data    = NULL;
    return 0;
}


/********************
 * vm_method_set
 ********************/
int
vm_method_set(vm_state_t *vm, char *name, vm_action_t handler, void *data)
{
    vm_method_t *m;
    
    if ((m = vm_method_lookup(vm, name)) == &default_method)
        return ENOENT;
    
    if (m->handler != NULL)
        return EEXIST;
    
    m->handler = handler;
    m->data    = data;
    return 0;
}


/********************
 * vm_method_lookup
 ********************/
vm_method_t *
vm_method_lookup(vm_state_t *vm, char *name)
{
    int i;
    
    for (i = 0; i < vm->nmethod; i++)
        if (!strcmp(name, vm->methods[i].name))
            return vm->methods + i;
    
    return &default_method;
}


/********************
 * vm_method_id
 ********************/
int
vm_method_id(vm_state_t *vm, char *name)
{
    vm_method_t *m = vm_method_lookup(vm, name);

    if (m == &default_method)
        return -1;
    
    return m->id;
}


/********************
 * vm_method_by_id
 ********************/
vm_method_t *
vm_method_by_id(vm_state_t *vm, int id)
{
    if (0 <= id && id < vm->nmethod)
        return vm->methods + id;
    
    return &default_method;
}


/********************
 * vm_method_default
 ********************/
vm_action_t
vm_method_default(vm_state_t *vm, vm_action_t handler, void **data)
{
    vm_action_t  old_handler = default_method.handler;
    void        *old_data    = default_method.data;

    default_method.handler = handler;
    
    if (data != NULL) {
        default_method.data = *data;
        *data               = old_data;
    }
    else
        default_method.data = NULL;
    
    return old_handler;

    (void)vm;
}


/********************
 * vm_method_call
 ********************/
int
vm_method_call(vm_state_t *vm, char *name, vm_method_t *m, int narg)
{
    vm_action_t       handler;
    void             *data;
    vm_stack_entry_t *args = vm_args(vm->stack, narg);
    vm_stack_entry_t  retval;
    int               status;

    if (args == NULL && narg > 0)
        VM_RAISE(vm, ENOENT,
                 "CALL: failed to pop %d args for %s", narg, m->name);
    
    handler = m->handler ? m->handler : default_method.handler;
    data    = m->handler ? m->data    : default_method.data;
    status  = handler(data, name, args, narg, &retval);
    vm_stack_cleanup(vm->stack, narg);
    
    if (status > 0)
        vm_push(vm->stack, retval.type, retval.v);

    return status;
}


/********************
 * vm_free_methods
 ********************/
void
vm_free_methods(vm_state_t *vm)
{
    int          i;
    vm_method_t *m;
    
    for (i = 0, m = vm->methods; i < vm->nmethod; i++, m++)
        FREE(m->name);
    
    FREE(vm->methods);
    
    vm->methods = NULL;
    vm->nmethod = 0;
}



/********************
 * vm_unknown_handler
 ********************/
static int
vm_unknown_handler(void *data, char *name,
                   vm_stack_entry_t *args, int narg, vm_stack_entry_t *retval)
{
    int i, j;

    printf("OOPS: call to unknown method %s\n", name);
    printf("OOPS: called with %d argument%s\n", narg, narg == 1 ? "" : "s");
    for (i = 0; i < narg; i++) {
        printf("OOPS: #%d: ", i);
        switch (args[i].type) {
        case VM_TYPE_INTEGER: printf("%d\n", args[i].v.i); break;
        case VM_TYPE_DOUBLE:  printf("%f\n", args[i].v.d); break;
        case VM_TYPE_STRING:  printf("%s\n", args[i].v.s); break;
        case VM_TYPE_GLOBAL:
            for (j = 0; j < args[i].v.g->nfact; j++) {
                printf("$");
                vm_fact_print(stdout, args[i].v.g->facts[j]);
                printf("\n");
            }
            break;
        default:
            printf("<unknown type 0x%x>\n", args[i].type);
        }
    }

    return ENOENT;
    
    (void)data;
    (void)retval;
}




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

