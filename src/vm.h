#ifndef __DRES_VM_H__
#define __DRES_VM_H__


enum {
    VM_TYPE_UNKNOWN = 0,
    VM_TYPE_INTEGER,
    VM_TYPE_DOUBLE,
    VM_TYPE_FLOAT = VM_TYPE_DOUBLE,
    VM_TYPE_STRING,
};


typedef union vm_value_s {
    double  d;                                /* VM_TYPE_DOUBLE  */
    int     i;                                /* VM_TYPE_INTEGER */
    char   *s;                                /* VM_TYPE_STRING */
    void   *p;                                /* VM_TYPE_* */
} vm_value_t;


typedef struct vm_stack_entry_s {
    vm_value_t v;
    int        type;
} vm_stack_entry_t;


typedef struct vm_stack_s {
    vm_stack_entry_t *entries;                /* actual stack entries */
    int               nentry;                 /* top of the stack */
    int               nalloc;                 /* size of the stack */
} vm_stack_t;



#endif /* __DRES_VM_H__ */



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
