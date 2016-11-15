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


int vm_dump_push   (uintptr_t **pc, char *buf, size_t size, int indent);
int vm_dump_pop    (uintptr_t **pc, char *buf, size_t size, int indent);
int vm_dump_filter (uintptr_t **pc, char *buf, size_t size, int indent);
int vm_dump_update (uintptr_t **pc, char *buf, size_t size, int indent);
int vm_dump_set    (uintptr_t **pc, char *buf, size_t size, int indent);
int vm_dump_get    (uintptr_t **pc, char *buf, size_t size, int indent);
int vm_dump_create (uintptr_t **pc, char *buf, size_t size, int indent);
int vm_dump_call   (uintptr_t **pc, char *buf, size_t size, int indent);
int vm_dump_cmp    (uintptr_t **pc, char *buf, size_t size, int indent);
int vm_dump_branch (uintptr_t **pc, char *buf, size_t size, int indent);
int vm_dump_debug  (uintptr_t **pc, char *buf, size_t size, int indent);
int vm_dump_halt   (uintptr_t **pc, char *buf, size_t size, int indent);
int vm_dump_invalid(uintptr_t **pc, char *buf, size_t size, int indent);
int vm_dump_replace(uintptr_t **pc, char *buf, size_t size, int indent);

/********************
 * vm_dump_chunk
 ********************/
int
vm_dump_chunk(vm_state_t *vm, char *buf, size_t size, int indent)
{
    uintptr_t    *pc;
    int           total, n;

    if (vm->ninstr <= 0)
        return 0;

    total = n = 0;
    pc    = vm->pc;
    while (pc) {
        n = vm_dump_instr(&pc, buf, size, indent);

        if (n > 0) {
            total += n;
            buf   += n;
            size  -= n;
        }
        else
            break;
    }
    
    if (n < 0)
        return n;
    else
        return total;
}


/********************
 * vm_dump_instr
 ********************/
int
vm_dump_instr(uintptr_t **pc, char *buf, size_t size, int indent)
{
    int n;
    
    switch ((vm_opcode_t)VM_OP_CODE(**pc)) {
    case VM_OP_PUSH:    n = vm_dump_push(pc, buf, size, indent);    break;
    case VM_OP_POP:     n = vm_dump_pop(pc, buf, size, indent);     break;
    case VM_OP_FILTER:  n = vm_dump_filter(pc, buf, size, indent);  break;
    case VM_OP_UPDATE:  n = vm_dump_update(pc, buf, size, indent);  break;
    case VM_OP_SET:     n = vm_dump_set(pc, buf, size, indent);     break;
    case VM_OP_GET:     n = vm_dump_get(pc, buf, size, indent);     break;
    case VM_OP_CREATE:  n = vm_dump_create(pc, buf, size, indent);  break;
    case VM_OP_CALL:    n = vm_dump_call(pc, buf, size, indent);    break;
    case VM_OP_CMP:     n = vm_dump_cmp(pc, buf, size, indent);     break;
    case VM_OP_BRANCH:  n = vm_dump_branch(pc, buf, size, indent);  break;
    case VM_OP_DEBUG:   n = vm_dump_debug(pc, buf, size, indent);   break;
    case VM_OP_HALT:    n = vm_dump_halt(pc, buf, size, indent);    break;
    case VM_OP_REPLACE: n = vm_dump_replace(pc, buf, size, indent);  break;
    default:            n = vm_dump_invalid(pc, buf, size, indent); *pc = 0x0;
    }
        
    return n;
}


/********************
 * vm_dump_push
 ********************/
int
vm_dump_push(uintptr_t **pc, char *buf, size_t size, int indent)
{
    uintptr_t     type = VM_PUSH_TYPE(**pc);
    uintptr_t     data = VM_PUSH_DATA(**pc);
    int           n, nsize;

    INDENT(indent);
    
    switch (type) {
    case VM_TYPE_INTEGER:
        n += snprintf(buf, size, "push %lld\n",
                     data ? data - 1 : (long long int)*(*pc + 1));
        nsize = data ? 1 : 2;
        break;

    case VM_TYPE_DOUBLE:
        n += snprintf(buf, size, "push %f\n", *(double *)(*pc + 1));
        nsize = 1 + VM_ALIGN_TO_INSTR(sizeof(double));
        break;

    case VM_TYPE_STRING:
        n += snprintf(buf, size, "push '%s'\n", (const char *)(*pc + 1));
        nsize = 1 + VM_ALIGN_TO_INSTR(data);
        break;

    case VM_TYPE_GLOBAL:
        n += snprintf(buf, size, "push global %s\n", (char *)(*pc + 1));
        nsize = 1 + VM_ALIGN_TO_INSTR(data);
        break;

    case VM_TYPE_LOCAL:
        n += snprintf(buf, size, "push locals %lld\n", (long long int)data);
        nsize = 1;
        break;
        
    default:
        n += snprintf(buf, size, "<invalid push instruction 0x%x>\n", type);
        nsize = 1;
    }

    *pc += nsize;
    
    return n;
}


/********************
 * vm_dump_pop
 ********************/
int
vm_dump_pop(uintptr_t **pc, char *buf, size_t size, int indent)
{
    uintptr_t type = VM_OP_ARGS(**pc);
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
    
    (*pc)++;

    return n;
}


/********************
 * vm_dump_filter
 ********************/
int
vm_dump_filter(uintptr_t **pc, char *buf, size_t size, int indent)
{
    uintptr_t nfield = VM_FILTER_NFIELD(**pc);
    int n;

    INDENT(indent);

    n += snprintf(buf, size, "filter %llu\n", (long long unsigned int)nfield);

    (*pc)++;

    return n;
}


/********************
 * vm_dump_update
 ********************/
int
vm_dump_update(uintptr_t **pc, char *buf, size_t size, int indent)
{
    uintptr_t nfield = VM_UPDATE_NFIELD(**pc);
    int n;

    INDENT(indent);

    n += snprintf(buf, size, "update %llu\n", (long long unsigned int)nfield);

    (*pc)++;

    return n;
}


/********************
 * vm_dump_replace
 ********************/
int
vm_dump_replace(uintptr_t **pc, char *buf, size_t size, int indent)
{
    uintptr_t nfield = VM_REPLACE_NFIELD(**pc);
    int n;

    INDENT(indent);

    n += snprintf(buf, size, "replace %llu\n", (long long unsigned int)nfield);

    (*pc)++;

    return n;
}


/********************
 * vm_dump_set
 ********************/
int
vm_dump_set(uintptr_t **pc, char *buf, size_t size, int indent)
{
    int n;

    INDENT(indent);

    if (VM_OP_ARGS(**pc) == VM_SET_FIELD)
        n += snprintf(buf, size, "set field\n");
    else
        n += snprintf(buf, size, "set global\n");
    
    (*pc)++;
    
    return n;
}


/********************
 * vm_dump_get
 ********************/
int
vm_dump_get(uintptr_t **pc, char *buf, size_t size, int indent)
{
    uintptr_t type = VM_OP_ARGS(**pc);
    int       n;

    INDENT(indent);
    
    if (type & VM_GET_FIELD)
        n += snprintf(buf, size, "get field\n");
    else if (type & VM_GET_LOCAL)
        n += snprintf(buf, size, "get local 0x%x\n", type & ~VM_GET_LOCAL);
    else
        n += snprintf(buf, size, "<invalid get instruction 0x%x\n", type);
    
    (*pc)++;
    
    return n;
}


/********************
 * vm_dump_create
 ********************/
int
vm_dump_create(uintptr_t **pc, char *buf, size_t size, int indent)
{
    uintptr_t nfield = VM_FILTER_NFIELD(**pc);
    int n;

    INDENT(indent);
    n += snprintf(buf, size, "create %llu\n", (long long unsigned int)nfield);

    (*pc)++;

    return n;
}
    

/********************
 * vm_dump_call
 ********************/
int
vm_dump_call(uintptr_t **pc, char *buf, size_t size, int indent)
{
    uintptr_t narg = VM_OP_ARGS(**pc);
    int n;

    INDENT(indent);
    n += snprintf(buf, size, "call %llu\n", (long long unsigned int)narg);
    
    (*pc)++;
    
    return n;
}


/********************
 * vm_dump_cmp
 ********************/
int
vm_dump_cmp(uintptr_t **pc, char *buf, size_t size, int indent)
{
    vm_relop_t  op = VM_OP_ARGS(**pc);
    char       *opstr;
    int         n;

    switch (op) {
    case VM_RELOP_EQ:  opstr = "=="; break;
    case VM_RELOP_NE:  opstr = "!="; break;
    case VM_RELOP_LT:  opstr = "<";  break;
    case VM_RELOP_LE:  opstr = "<="; break;
    case VM_RELOP_GT:  opstr = ">";  break;
    case VM_RELOP_GE:  opstr = ">="; break;
    case VM_RELOP_NOT: opstr = "!";  break;
    default:           opstr = "??"; break;
    }
    
    INDENT(indent);
    n += snprintf(buf, size, "cmp %s\n", opstr);
    
    (*pc)++;
    
    return n;
}


/********************
 * vm_dump_branch
 ********************/
int
vm_dump_branch(uintptr_t **pc, char *buf, size_t size, int indent)
{
    uintptr_t brtype, brdiff;
    int       n;
    char     *type;
    
    brtype = VM_BRANCH_TYPE(**pc);
    brdiff = VM_BRANCH_DIFF(**pc);

    switch ((vm_branch_t)brtype) {
    case VM_BRANCH:    type = "";   break;
    case VM_BRANCH_EQ: type = " eq"; break;
    case VM_BRANCH_NE: type = " ne"; break;
    default:           type = " ??"; break;
    }
    
    INDENT(indent);
    n += snprintf(buf, size, "branch%s %lld\n", type, (long long int)brdiff);

    (*pc)++;

    return n;
}


/********************
 * vm_dump_debug
 ********************/
int
vm_dump_debug(uintptr_t **pc, char *buf, size_t size, int indent)
{
    const char *info  = (const char *)(*pc + 1);
    int         len   = VM_DEBUG_LEN(**pc);
    int         nsize = 1 + VM_ALIGN_TO_INSTR(len);
    int         n;

    INDENT(indent);
    n += snprintf(buf, size, "debug info \"%s\"\n", info);
    
    *pc += nsize;
    
    return n;
}


/********************
 * vm_dump_halt
 ********************/
int
vm_dump_halt(uintptr_t **pc, char *buf, size_t size, int indent)
{
    int n;
    
    INDENT(indent);
    n += snprintf(buf, size, "halt\n");
    
    *pc = 0x0;

    return n;
}


/********************
 * vm_dump_invalid
 ********************/
int
vm_dump_invalid(uintptr_t **pc, char *buf, size_t size, int indent)
{
    int n;
    
    INDENT(indent);
    n += snprintf(buf, size, "invalid instruction 0x%x", **pc);

    return n;
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */

