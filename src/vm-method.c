#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "mm.h"
#include "vm.h"


static int vm_default_handler(char *name,
                              vm_stack_entry_t *args, int narg,
                              vm_stack_entry_t *retval);

static vm_method_t unknown_handler = { "default", vm_default_handler };

/********************
 * vm_method_lookup
 ********************/
vm_method_t *
vm_method_lookup(vm_state_t *vm, char *name)
{
    return &unknown_handler;
}


/********************
 * vm_method_by_id
 ********************/
vm_method_t *
vm_method_by_id(vm_state_t *vm, int id)
{
    return &unknown_handler;
}


/********************
 * vm_method_default
 ********************/
vm_method_t *
vm_method_default(vm_state_t *vm)
{
    return NULL;
}


/********************
 * vm_method_call
 ********************/
int
vm_method_call(vm_state_t *vm, vm_method_t *m, int narg)
{
    vm_stack_entry_t *args = vm_args(vm->stack, narg);
    vm_stack_entry_t  retval;
    vm_value_t        arg;
    int               status, i, type;

    if (args == NULL)
        VM_EXCEPTION(vm, "CALL: failed to pop %d args for %s", narg, m->name);
    
    status = m->handler(m->name, args, narg, &retval);
    
    for (i = 0; i < narg; i++) {
        if ((type = vm_pop(vm->stack, &arg)) == VM_TYPE_GLOBAL)
            vm_global_free(arg.g);
    }
    
    if (!status)
        vm_push(vm->stack, retval.type, retval.v);

    return status;
}


/********************
 * vm_default_handler
 ********************/
static int
vm_default_handler(char *name,
                   vm_stack_entry_t *args, int narg,
                   vm_stack_entry_t *retval)
{
    int i, type;

    printf("OOPS: call to unknown method %s\n", name);
    printf("OOPS: called with %d argument%s\n", narg, narg == 1 ? "" : "s");
    for (i = 0; i < narg; i++) {
        printf("OOPS: #%d: ", i);
        switch (args[i].type) {
        case VM_TYPE_INTEGER: printf("%d\n", args[i].v.i); break;
        case VM_TYPE_DOUBLE:  printf("%f\n", args[i].v.d); break;
        case VM_TYPE_STRING:  printf("'%s'\n", args[i].v.s); break;
        case VM_TYPE_GLOBAL:  printf("a global...\n"); break;
        default:              printf("<unknown type 0x%x>\n", type);
        }
    }

    return ENOENT;
}




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

