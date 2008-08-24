#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include <dres/mm.h>
#include <dres/vm.h>

#define SPACE "                                "
#define INDENT(i) do {                                          \
        n = snprintf(buf, size, "%*.*s", (i), (i), SPACE);      \
        buf  += n;                                              \
        size -= n;                                              \
    } while (0)


int vm_dump_push  (vm_state_t *vm, char *buf, size_t size, int indent);
int vm_dump_pop   (vm_state_t *vm, char *buf, size_t size, int indent);
int vm_dump_filter(vm_state_t *vm, char *buf, size_t size, int indent);
int vm_dump_update(vm_state_t *vm, char *buf, size_t size, int indent);
int vm_dump_set   (vm_state_t *vm, char *buf, size_t size, int indent);
int vm_dump_get   (vm_state_t *vm, char *buf, size_t size, int indent);
int vm_dump_create(vm_state_t *vm, char *buf, size_t size, int indent);
int vm_dump_call  (vm_state_t *vm, char *buf, size_t size, int indent);


/********************
 * vm_dump_chunk
 ********************/
int
vm_dump_chunk(vm_state_t *vm, char *buf, size_t size, int indent)
{
    int total, n;
    
    total = n = 0;
    while (vm->ninstr > 0) {
        switch (VM_OP_CODE(*vm->pc)) {
        case VM_OP_PUSH:   n = vm_dump_push(vm, buf, size, indent);   break;
        case VM_OP_POP:    n = vm_dump_pop(vm, buf, size, indent);    break;
        case VM_OP_FILTER: n = vm_dump_filter(vm, buf, size, indent); break;
        case VM_OP_UPDATE: n = vm_dump_update(vm, buf, size, indent); break;
        case VM_OP_SET:    n = vm_dump_set(vm, buf, size, indent);    break;
        case VM_OP_GET:    n = vm_dump_get(vm, buf, size, indent);    break;
        case VM_OP_CREATE: n = vm_dump_create(vm, buf, size, indent); break;
        case VM_OP_CALL:   n = vm_dump_call(vm, buf, size, indent);   break;
        default: VM_EXCEPTION(vm, EINVAL, "invalid instruction 0x%x", *vm->pc);
        }
        
        if (n > 0) {
            total += n;
            buf   += n;
            size  -= n;
        }
    }
    
    if (n < 0)
        return n;
    else
        return total;
}


/********************
 * vm_dump_push
 ********************/
int
vm_dump_push(vm_state_t *vm, char *buf, size_t size, int indent)
{
    unsigned int  type = VM_PUSH_TYPE(*vm->pc);
    unsigned int  data = VM_PUSH_DATA(*vm->pc);
    int           n, nsize;

    INDENT(indent);
    
    switch (type) {
    case VM_TYPE_INTEGER:
        n += snprintf(buf, size, "push %d\n",
                     data ? data - 1 : (int)*(vm->pc + 1));
        nsize = data ? 1 : 2;
        break;

    case VM_TYPE_DOUBLE:
        n += snprintf(buf, size, "push %f\n", *(double *)(vm->pc + 1));
        nsize = 1 + sizeof(double) / sizeof(int);
        break;

    case VM_TYPE_STRING:
        n += snprintf(buf, size, "push '%s'\n", (char *)(vm->pc + 1));
        nsize = 1 + VM_ALIGN_TO(data, sizeof(int)) / sizeof(int);
        break;

    case VM_TYPE_GLOBAL:
        n += snprintf(buf, size, "push global %s\n", (char *)(vm->pc + 1));
        nsize = 1 + VM_ALIGN_TO(data, sizeof(int)) / sizeof(int);;
        break;

    case VM_TYPE_LOCAL:
        n += snprintf(buf, size, "push locals %d\n", data);
        nsize = 1;
        break;
        
    default:
        n += snprintf(buf, size, "<invalid push instruction 0x%x>\n", type);
        nsize = 1;
    }

    vm->ninstr--;
    vm->pc     += nsize;
    vm->nsize  -= nsize * sizeof(int);
    
    return n;
}


/********************
 * vm_dump_pop
 ********************/
int
vm_dump_pop(vm_state_t *vm, char *buf, size_t size, int indent)
{
    int type = VM_OP_ARGS(*vm->pc);
    int n;

    INDENT(indent);
        
    switch (type) {
    case VM_POP_LOCALS:
        n += snprintf(buf, size, "pop locals\n");
        break;
        
    case VM_POP_DISCARD:
        n += snprintf(buf, size, "pop global\n");
        break;
        
    default:
        n += snprintf(buf, size, "<invalid POP instructions 0x%x>\n", type);
    }
    
    vm->ninstr--;
    vm->pc++;
    vm->nsize -= sizeof(int);

    return n;
}


/********************
 * vm_dump_filter
 ********************/
int
vm_dump_filter(vm_state_t *vm, char *buf, size_t size, int indent)
{
    int nfield = VM_FILTER_NFIELD(*vm->pc);
    int n;

    INDENT(indent);

    n += snprintf(buf, size, "filter %d\n", nfield);

    vm->ninstr--;
    vm->pc++;
    vm->nsize -= sizeof(int);

    return n;
}


/********************
 * vm_dump_update
 ********************/
int
vm_dump_update(vm_state_t *vm, char *buf, size_t size, int indent)
{
    int nfield = VM_UPDATE_NFIELD(*vm->pc);
    int n;

    INDENT(indent);

    n += snprintf(buf, size, "update %d\n", nfield);

    vm->ninstr--;
    vm->pc++;
    vm->nsize -= sizeof(int);

    return n;
}


/********************
 * vm_dump_set
 ********************/
int
vm_dump_set(vm_state_t *vm, char *buf, size_t size, int indent)
{
    int n;

    INDENT(indent);

    if (VM_OP_ARGS(*vm->pc) == VM_SET_FIELD)
        n += snprintf(buf, size, "set field\n");
    else
        n += snprintf(buf, size, "set global\n");
    
    vm->ninstr--;
    vm->pc++;
    vm->nsize -= sizeof(int);
    
    return n;
}


/********************
 * vm_dump_get
 ********************/
int
vm_dump_get(vm_state_t *vm, char *buf, size_t size, int indent)
{
    int   type = VM_OP_ARGS(*vm->pc);
    int   n;

    INDENT(indent);
    
    if (type & VM_GET_FIELD)
        n += snprintf(buf, size, "get field\n");
    else if (type & VM_GET_LOCAL)
        n += snprintf(buf, size, "get local 0x%x\n", type & ~VM_GET_LOCAL);
    else
        n += snprintf(buf, size, "<invalid get instruction 0x%x\n", type);
    
    vm->ninstr--;
    vm->pc++;
    vm->nsize -= sizeof(int);
    
    return n;
}


/********************
 * vm_dump_create
 ********************/
int
vm_dump_create(vm_state_t *vm, char *buf, size_t size, int indent)
{
    int nfield = VM_FILTER_NFIELD(*vm->pc);
    int n;

    INDENT(indent);
    n += snprintf(buf, size, "create %d\n", nfield);

    vm->ninstr--;
    vm->pc++;
    vm->nsize -= sizeof(int);

    return n;
}
    

/********************
 * vm_dump_call
 ********************/
int
vm_dump_call(vm_state_t *vm, char *buf, size_t size, int indent)
{
    int narg = VM_OP_ARGS(*vm->pc);
    int n;

    INDENT(indent);
    n += snprintf(buf, size, "call %d\n", narg);
    
    vm->ninstr--;
    vm->pc++;
    vm->nsize -= sizeof(int);
    
    return n;
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

