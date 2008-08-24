#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <setjmp.h>

#include <dres/mm.h>
#include <dres/vm.h>

#define DEFAULT_STACK_SIZE 32


/********************
 * vm_init
 ********************/
int
vm_init(vm_state_t *vm, int stack_size)
{
    memset(vm, 0, sizeof(*vm));

    if (stack_size <= 0)
        stack_size = DEFAULT_STACK_SIZE;

    if ((vm->stack = vm_stack_new(stack_size)) == NULL)
        return ENOMEM;
    
    return 0;
}


/********************
 * vm_exec
 ********************/
int
vm_exec(vm_state_t *vm, vm_chunk_t *code)
{
    int status;


    vm->chunk  = code;
    vm->pc     = code->instrs;
    vm->ninstr = code->ninstr;
    vm->nsize  = code->nsize;
    
#if 0
    catch.prev  = vm->catch;
    catch.depth = vm->stack ? vm->stack->nentry : 0;
    vm->catch = &catch;
    
    if ((status = setjmp(catch.location)) != 0) {
        printf("*** VM exception occured.\n");
        printf("*** I should clean up the stack till %d entries...\n",
               catch.depth);
        vm->catch = catch.prev;
        return -status;
    }
#endif

    VM_CATCH_PUSH(vm);
    status = vm_run(vm);
    VM_CATCH_POP(vm);
    
    /*printf("*** stack depth after vm_exec: %d\n", vm->stack->nentry);*/

    return status;         /* should be 0 because of our exceptions */
}






/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
