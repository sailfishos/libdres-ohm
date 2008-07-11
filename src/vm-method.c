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
    
    if (vm_method_lookup(vm, name) != &default_method)
        return EEXIST;
    
    if (REALLOC_ARR(vm->methods, vm->nmethod, vm->nmethod + 1) == NULL)
        return ENOMEM;

    m = vm->methods + vm->nmethod;

    m->name    = STRDUP(name);
    m->id      = vm->nmethod;
    m->handler = handler;
    m->data    = data;

    if (m->name == NULL)
        return ENOMEM;

    vm->nmethod++;

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
vm_method_default(vm_state_t *vm, vm_action_t handler)
{
    vm_action_t old = default_method.handler;

    default_method.handler = handler;

    return old;

    (void)vm;
}


/********************
 * vm_method_call
 ********************/
int
vm_method_call(vm_state_t *vm, char *name, vm_method_t *m, int narg)
{
    vm_stack_entry_t *args = vm_args(vm->stack, narg);
    vm_stack_entry_t  retval;
    vm_value_t        arg;
    int               status, i, type;

    if (args == NULL)
        VM_EXCEPTION(vm, "CALL: failed to pop %d args for %s", narg, m->name);
    
    status = m->handler(m->data, name, args, narg, &retval);
    
    for (i = 0; i < narg; i++) {
        if ((type = vm_pop(vm->stack, &arg)) == VM_TYPE_GLOBAL)
            vm_global_free(arg.g);
    }
    
    if (!status)
        vm_push(vm->stack, retval.type, retval.v);

    return status;
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
                vm_fact_print(args[i].v.g->facts[j]);
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

