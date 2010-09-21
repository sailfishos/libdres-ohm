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



#define STACK_SIZE 50

#define fatal(ec, fmt, args...) do {                                        \
        fprintf(stderr, "%s: fatal error: "fmt"\n", __FUNCTION__, ## args); \
        exit(ec);                                                           \
    } while (0)


int DBG_VM = 0;


/********************
 * dump_global
 ********************/
void
dump_global(vm_global_t *g)
{
    char *s;
    int   i;
    
    printf("global with %d fact%s\n", g->nfact, g->nfact == 1 ? "" : "s");
    for (i = 0; i < g->nfact; i++) {
        s = ohm_structure_to_string(OHM_STRUCTURE(g->facts[i]));
        printf("  #%d: ", i); vm_fact_print(stdout, g->facts[i]); printf("\n");
    }
}


/********************
 * dump_factstore
 ********************/
void
dump_factstore(void)
{
    char *s;
    
    s = ohm_fact_store_to_string(ohm_fact_store_get_fact_store());
    printf("factstore = %s\n", s);
    g_free(s);
}


/********************
 * stack_pop_all
 ********************/
int
stack_pop_all(vm_stack_t *stack)
{
    int          i, n, type;
    double       d;
    char        *s;
    vm_global_t *g;

    i = 0;
    while ((type = vm_type(stack)) != VM_TYPE_UNKNOWN) {
        switch (type) {
        case VM_TYPE_INTEGER:
            n = vm_pop_int(stack);
            printf("#%d: popped %d (0x%x) from the stack...\n", i, n, n);
            break;
            
        case VM_TYPE_DOUBLE:
            d = vm_pop_double(stack);
            printf("#%d: popped %f from the stack...\n", i, d);
            break;
            
        case VM_TYPE_STRING:
            s = vm_pop_string(stack);
            printf("#%d: popped %s from the stack...\n", i, s);
            break;

        case VM_TYPE_GLOBAL:
            g = vm_pop_global(stack);
            printf("#%d: popped global %p from the stack\n", i, g);
            dump_global(g);
            vm_global_free(g);
            break;
            
        default:
            fatal(EINVAL, "#%d: unexpected object on stack", i);
            return EINVAL;
        }
        
        vm_stack_trim(stack, 0);
        i++;
    }
    
    return 0;
}


/********************
 * stack_test
 ********************/
int
stack_test(void)
{
    vm_stack_t *stack  = vm_stack_new(1);
    int         i, err = 0;

    printf("*** [%s] ***\n", __FUNCTION__);

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

    stack_pop_all(stack);
    vm_stack_del(stack);
    
    return 0;
}


/********************
 * chunk_test
 ********************/
int
chunk_test(void)
{
    vm_stack_t *stack = vm_stack_new(20);
    vm_chunk_t *chunk = vm_chunk_new(200);
    vm_state_t  vm;
    int         err;

    printf("*** [%s] ***\n", __FUNCTION__);

    /* allocate initial stack and code buffer */
    if (stack == NULL || chunk == NULL)
        fatal(ENOMEM, "failed to allocate stack and/or code");

    /* generate some code */
    VM_INSTR_PUSH_INT(chunk, cgfail, err, 10);
    VM_INSTR_PUSH_DOUBLE(chunk, cgfail, err, 3.141);
    VM_INSTR_PUSH_INT(chunk, cgfail, err, 0xb19b00b);
    VM_INSTR_PUSH_DOUBLE(chunk, cgfail, err, 9.81);
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "this is a test string...");


    /* execute the code */
    vm.stack = stack;
    vm.chunk = chunk;

    vm.pc     = chunk->instrs;
    vm.ninstr = chunk->ninstr;
    vm.nsize  = chunk->nsize;

    vm_run(&vm);
    stack_pop_all(stack);
    
    vm_stack_del(stack);
    vm_chunk_del(chunk);

    return 0;


 cgfail:
    fatal(err, "code generation failed");
}


/********************
 * filter_test
 ********************/
int
filter_test(void)
{
    vm_stack_t *stack = vm_stack_new(20);
    vm_chunk_t *chunk = vm_chunk_new(10);
    vm_state_t  vm;
    int         err;

    printf("*** [%s] ***\n", __FUNCTION__);
    
    if (stack == NULL || chunk == NULL)
        fatal(ENOMEM, "failed to allocate stack and/or code");

    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "bar");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foo");
    VM_INSTR_PUSH_INT(chunk, cgfail, err, 123);
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foobar");
    VM_INSTR_CREATE(chunk, cgfail, err, 2);
    
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "bar");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foo");
    VM_INSTR_PUSH_INT(chunk, cgfail, err, 123);
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foobar");
    VM_INSTR_FILTER(chunk, cgfail, err, 2);

    
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "bar");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foo");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "barfoo");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foobar");
    VM_INSTR_CREATE(chunk, cgfail, err, 2);
    
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foo");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "bar");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "xyzzy");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foobar");
    VM_INSTR_FILTER(chunk, cgfail, err, 2);

    vm.stack = stack;
    vm.chunk = chunk;
    
    vm.pc     = chunk->instrs;
    vm.ninstr = chunk->ninstr;
    vm.nsize  = chunk->nsize;

    vm_run(&vm);

    stack_pop_all(vm.stack);
    
    vm_stack_del(stack);
    vm_chunk_del(chunk);

    return 0;

 cgfail:
    fatal(err, "code generation failed");
}


/********************
 * set_test
 ********************/
int
set_test(void)
{
    vm_stack_t *stack = vm_stack_new(20);
    vm_chunk_t *chunk = vm_chunk_new(10);
    vm_state_t  vm;
    int         err;

    printf("*** [%s] ***\n", __FUNCTION__);
    
    if (stack == NULL || chunk == NULL)
        fatal(ENOMEM, "failed to allocate stack and/or code");

    /* $a = { foo:'bar', foobar:123 } */
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "bar");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foo");
    VM_INSTR_PUSH_INT(chunk, cgfail, err, 123);
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foobar");
    VM_INSTR_CREATE(chunk, cgfail, err, 2);
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "a");
    VM_INSTR_SET(chunk, cgfail, err);

    /* $b = { foo: 'bar', foobar: 'barfoo' } */
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "bar");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foo");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "barfoo");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foobar");
    VM_INSTR_CREATE(chunk, cgfail, err, 2);
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "b");
    VM_INSTR_SET(chunk, cgfail, err);

    /* $c = $a */
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "a");
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "c");
    VM_INSTR_SET(chunk, cgfail, err);

    /* $d = $b */
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "b");
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "d");
    VM_INSTR_SET(chunk, cgfail, err);

    /* $e = $a */
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "a");
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "e");
    VM_INSTR_SET(chunk, cgfail, err);
    
    /* $e:foobar = 456 */
    VM_INSTR_PUSH_INT(chunk, cgfail, err, 456);
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "e");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foobar");
    VM_INSTR_SET_FIELD(chunk, cgfail, err);

    vm.stack = stack;
    vm.chunk = chunk;
    
    vm.pc     = chunk->instrs;
    vm.ninstr = chunk->ninstr;
    vm.nsize  = chunk->nsize;

    vm_run(&vm);

    stack_pop_all(vm.stack);

    dump_factstore();
    
    vm_stack_del(stack);
    vm_chunk_del(chunk);

    return 0;

 cgfail:
    fatal(err, "code generation failed");
}


/********************
 * echo_handler
 ********************/
int
echo_handler(void *data, char *name,
             vm_stack_entry_t *args, int narg, vm_stack_entry_t *retval)
{
    int i;

    printf("%s data: %p (%s)\n", __FUNCTION__, data, data ? (char *)data : "");

    for (i = 0; i < narg; i++) {
        switch (args[i].type) {
        case VM_TYPE_INTEGER: printf("%d", args[i].v.i); break;
        case VM_TYPE_DOUBLE:  printf("%f", args[i].v.d); break;
        case VM_TYPE_STRING:  printf("%s", args[i].v.s); break;
        case VM_TYPE_GLOBAL:  vm_global_print(stdout, args[i].v.g); break;
        default:              printf("<unknown type 0x%x>", args[i].type);
        }
        printf(" ");
    }
    printf("\n");
    
    retval->type = VM_TYPE_INTEGER;
    retval->v.i  = 0;
    
    return 0;
    
    (void)name;
}


/********************
 * call_test
 ********************/
int
call_test(void)
{
    vm_stack_t *stack = vm_stack_new(20);
    vm_chunk_t *chunk = vm_chunk_new(10);
    vm_state_t  vm;
    int         err;

    printf("*** [%s] ***\n", __FUNCTION__);

    memset(&vm, 0, sizeof(vm));
    
    if (stack == NULL || chunk == NULL)
        fatal(ENOMEM, "failed to allocate stack and/or code");

    if ((err = vm_method_add(&vm, "echo", echo_handler, "foo")) != 0)
        fatal(err, "failed to register echo handler");

    /* $a = { foo:'bar', foobar:123 } */
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "bar");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foo");
    VM_INSTR_PUSH_INT(chunk, cgfail, err, 123);
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foobar");
    VM_INSTR_CREATE(chunk, cgfail, err, 2);
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "a");
    VM_INSTR_SET(chunk, cgfail, err);

    /* $b = { foo: 'bar', foobar: 'barfoo' } */
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "bar");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foo");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "barfoo");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foobar");
    VM_INSTR_CREATE(chunk, cgfail, err, 2);
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "b");
    VM_INSTR_SET(chunk, cgfail, err);

    /* $c = $a */
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "a");
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "c");
    VM_INSTR_SET(chunk, cgfail, err);

    /* $d = $b */
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "b");
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "d");
    VM_INSTR_SET(chunk, cgfail, err);

    /* $e = $a */
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "a");
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "e");
    VM_INSTR_SET(chunk, cgfail, err);
    
    /* $e:foobar = 456 */
    VM_INSTR_PUSH_INT(chunk, cgfail, err, 456);
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "e");
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "foobar");
    VM_INSTR_SET_FIELD(chunk, cgfail, err);

    /* echo($a, $b, $c, $d, $e, 1, 2.0, 'three') */
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "a");
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "b");
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "c");
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "d");
    VM_INSTR_PUSH_GLOBAL(chunk, cgfail, err, "e");
    VM_INSTR_PUSH_INT(chunk, cgfail, err, 1);
    VM_INSTR_PUSH_DOUBLE(chunk, cgfail, err, 2.0);
    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "three");

    VM_INSTR_PUSH_STRING(chunk, cgfail, err, "echo");
    VM_INSTR_CALL(chunk, cgfail, err, 8);

    vm.stack = stack;
    vm.chunk = chunk;
    
    vm.pc     = chunk->instrs;
    vm.ninstr = chunk->ninstr;
    vm.nsize  = chunk->nsize;

    vm_run(&vm);

    stack_pop_all(vm.stack);

    dump_factstore();
    
    vm_stack_del(stack);
    vm_chunk_del(chunk);

    return 0;

 cgfail:
    fatal(err, "code generation failed");
}



int
main(int argc, char *argv[])
{

    g_type_init();

    stack_test();
    chunk_test();
    filter_test();
    set_test();
    call_test();

    return 0;

    (void)argc;
    (void)argv;
}






/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

