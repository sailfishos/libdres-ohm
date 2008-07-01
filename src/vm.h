#ifndef __DRES_VM_H__
#define __DRES_VM_H__

#include <string.h>

#include <prolog/ohm-fact.h>


/*
 * miscallenous macros
 */

#define VM_ALIGN_TO(n, a) (((n) + ((a)-1)) & ~((a)-1))


/*
 * VM stack
 */

enum {
    VM_TYPE_UNKNOWN = 0,
    VM_TYPE_INTEGER,                          /* signed 32-bit integer */
    VM_TYPE_DOUBLE,                           /* double prec. floating */
    VM_TYPE_FLOAT = VM_TYPE_DOUBLE,           /* our foating is double */
    VM_TYPE_STRING,                           /* a \0-terminated string */
    VM_TYPE_FACTS,                            /* an array of facts */
    VM_TYPE_GLOBAL = VM_TYPE_FACTS,           /* globals are facts */
};


typedef struct vm_global_s {
    int      nfact;
    OhmFact *facts[0];
} vm_global_t;


typedef union vm_value_s {
    double       d;                           /* VM_TYPE_DOUBLE  */
    int          i;                           /* VM_TYPE_INTEGER */
    char        *s;                           /* VM_TYPE_STRING */
    vm_global_t *g;                           /* VM_TYPE_GLOBAL */
} vm_value_t;


typedef struct vm_stack_entry_s {
    vm_value_t v;                             /* actual value on the stack */
    int        type;                          /* type of the value */
} vm_stack_entry_t;


typedef struct vm_stack_s {
    vm_stack_entry_t *entries;                /* actual stack entries */
    int               nentry;                 /* top of the stack */
    int               nalloc;                 /* size of the stack */
} vm_stack_t;



/*
 * VM instructions
 */

enum {
    VM_OP_UNKNOWN = 0,
    VM_OP_PUSH,                               /* push a value on the stack */
    VM_OP_FILTER,                             /* global filtering */
    VM_OP_SET,                                /* global assignment */
    VM_OP_CREATE,                             /* global creation */
    VM_OP_CALL,                               /* function call */
};


#define VM_OP_CODE(instr)      ((instr) & 0xff)
#define VM_OP_ARGS(instr)      ((instr) >> 8)
#define VM_INSTR(opcode, args) ((opcode) | (args << 8))


/*
 * PUSH instructions
 */

#define VM_PUSH_TYPE(instr) (VM_OP_ARGS(instr) & 0xff)
#define VM_PUSH_DATA(instr) (VM_OP_ARGS(instr) >> 8)
#define VM_PUSH_INSTR(t, d)  VM_INSTR(VM_OP_PUSH, (((d) << 8) | ((t) & 0xff)))

#define VM_INSTR_PUSH_INT(c, errlbl, ec, val) do {                      \
        if (0 <= val && val < 0xfffe) {                                 \
            unsigned int instr;                                         \
            instr = VM_PUSH_INSTR(VM_TYPE_INTEGER, val + 1);            \
            ec = vm_chunk_add(c, &instr, 1, sizeof(instr));             \
            if (ec)                                                     \
                goto errlbl;                                            \
        }                                                               \
        else {                                                          \
            unsigned int instr[2];                                      \
            instr[0] = VM_PUSH_INSTR(VM_TYPE_INTEGER, 0);               \
            instr[1] = val;                                             \
            ec = vm_chunk_add(c, instr, 1, sizeof(instr));              \
            if (ec)                                                     \
                goto errlbl;                                            \
        }                                                               \
    } while (0)

#define VM_INSTR_PUSH_DOUBLE(c, errlbl, ec, val) do {                   \
        unsigned  instr[1 + sizeof(double)/sizeof(int)];                \
        double   *dp = (double *)&instr[1];                             \
        instr[0] = VM_PUSH_INSTR(VM_TYPE_DOUBLE, 0);                    \
        *dp      = val;                                                 \
        ec       = vm_chunk_add(c, instr, 1, sizeof(instr));            \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)

#define VM_INSTR_PUSH_STRING(c, errlbl, ec, val) do {                   \
        int           len = strlen(val) + 1;                            \
        int           n   = VM_ALIGN_TO(len, sizeof(int))/sizeof(int);  \
        unsigned int  instr[1 + n];                                     \
        instr[0] = VM_PUSH_INSTR(VM_TYPE_STRING, len);                  \
        strcpy((char *)(instr + 1), val);                               \
        /* could pad here with zeros if (len & 0x3) */                  \
        ec = vm_chunk_add(c, instr, 1, sizeof(instr));                  \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)


/*
 * FILTER instructions
 */

#define VM_FILTER_NFIELD(instr) VM_OP_ARGS(instr)

#define VM_INSTR_FILTER(c, errlbl, ec, n) do {                          \
        unsigned int instr;                                             \
        instr = VM_INSTR(VM_OP_FILTER, n);                              \
        ec = vm_chunk_add(c, &instr, 1, sizeof(instr));                 \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)


/*
 * CREATE instructions
 */

#define VM_CREATE_NFIELD(instr) VM_OP_ARGS(instr)

#define VM_INSTR_CREATE(c, errlbl, ec, n) do {                          \
        unsigned int instr;                                             \
        instr = VM_INSTR(VM_OP_CREATE, n);                              \
        ec = vm_chunk_add(c, &instr, 1, sizeof(instr));                 \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)

/*
 * SET instructions
 */

/*
 * CALL instructions
 */


/*
 * a chunk of VM instructions
 */

typedef struct vm_chunk_s {
    unsigned int *instrs;                    /* actual VM instructions */
    int           ninstr;                    /* number of instructions */
    int           nsize;                     /* code size in bytes */
    int           nleft;                     /* number of bytes free */
} vm_chunk_t;


/*
 * VM state
 */

typedef struct vm_state_s {
    vm_stack_t    *stack;                     /* VM stack */
    vm_chunk_t    *chunk;                     /* code being executed */
    
    unsigned int  *pc;                        /* program counter */
    int            ninstr;                    /* # of instructions left */
    int            nsize;                     /* of code left */
} vm_state_t;



/*
 * VM exceptions
 */

/* XXX FIXME: We need decent exception handling. Perhaps this should
 *            push and error (of type VM_TYPE_ERROR) on the stack and
 *            return with an error code (or do a longjmp to a jmp_buf
 *            (eg. vm->exception)...
 */
#define VM_EXCEPTION(vm, fmt, args...) do {                             \
        printf("%s: fatal VM error: "fmt"\n", __FUNCTION__, ## args);   \
        fflush(stdout);                                                 \
        exit(1);                                                        \
    } while(0)





/* vm-stack.c */
vm_stack_t *vm_stack_new (int size);
void        vm_stack_del (vm_stack_t *s);
int         vm_stack_grow(vm_stack_t *s, int n);
int         vm_stack_trim(vm_stack_t *s, int n);

int vm_type       (vm_stack_t *s);
int vm_push_int   (vm_stack_t *s, int i);
int vm_push_double(vm_stack_t *s, double d);
int vm_push_string(vm_stack_t *s, char *str);
int vm_push_global(vm_stack_t *s, vm_global_t *g);


int         vm_pop (vm_stack_t *s, vm_value_t *value);
int         vm_peek(vm_stack_t *s, int idx, vm_value_t *value);

int         vm_pop_int   (vm_stack_t *s);
double      vm_pop_double(vm_stack_t *s);
char        *vm_pop_string(vm_stack_t *s);
vm_global_t *vm_pop_global(vm_stack_t *s);

/* vm-instr.c */
vm_chunk_t   *vm_chunk_new (int ninstr);
void          vm_chunk_del (vm_chunk_t *chunk);
unsigned int *vm_chunk_grow(vm_chunk_t *c, int nsize);
int           vm_chunk_add (vm_chunk_t *c,
                            unsigned int *code, int ninstr, int nsize);

int vm_run(vm_state_t *vm);

/* vm-global.c */
int  vm_global_lookup(char *name, vm_global_t **gp);
void vm_global_free  (vm_global_t *g);


#endif /* __DRES_VM_H__ */



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
