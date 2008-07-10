#ifndef __DRES_VM_H__
#define __DRES_VM_H__

#include <string.h>

#include <ohm/ohm-fact.h>


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
    VM_TYPE_LOCAL,                            /* local variables */
    VM_TYPE_FACTS,                            /* an array of facts */
    VM_TYPE_GLOBAL = VM_TYPE_FACTS,           /* globals are facts */
};


#define VM_UNNAMED_GLOBAL "__vm_global"       /* an unnamed global */
#define VM_GLOBAL_IS_NAME(g) ((g)->name != NULL && (g)->nfact == 0)
#define VM_GLOBAL_IS_ORPHAN(g)                                     \
    ((g)->nfact == 1 &&                                            \
     !strcmp(ohm_structure_get_name(OHM_STRUCTURE((g)->facts[0])), \
             VM_UNNAMED_GLOBAL))
typedef struct vm_global_s {
    char    *name;                            /* for free-hanging facts */
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


typedef struct vm_local_s vm_local_t;

struct vm_local_s {
    vm_value_t  value;
    vm_local_t *next;
    
};


typedef struct vm_scope_s vm_scope_t;

struct vm_scope_s {
    GHashTable *variables;                    /* variables */
    vm_scope_t *parent;                       /* parent scope */
};


/*
 * VM instructions
 */

enum {
    VM_OP_UNKNOWN = 0,
    VM_OP_PUSH,                               /* push a value on the stack */
    VM_OP_FILTER,                             /* global filtering */
    VM_OP_SET,                                /* global assignment */
    VM_OP_GET,                                /* global/local evaluation */ 
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

#define VM_INSTR_PUSH_GLOBAL(c, errlbl, ec, val) do {                   \
        int           len = strlen(val) + 1;                            \
        int           n   = VM_ALIGN_TO(len, sizeof(int))/sizeof(int);  \
        unsigned int  instr[1 + n];                                     \
        instr[0] = VM_PUSH_INSTR(VM_TYPE_GLOBAL, len);                  \
        strcpy((char *)(instr + 1), val);                               \
        /* could pad here with zeros if (len & 0x3) */                  \
        ec = vm_chunk_add(c, instr, 1, sizeof(instr));                  \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)

#define VM_INSTR_PUSH_LOCALS(c, errlbl, ec, nvar) do {                  \
        unsigned int  instr;                                            \
        instr = VM_PUSH_INSTR(VM_TYPE_LOCAL, n);                        \
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

enum {
    VM_SET_NONE  = 0x0,
    VM_SET_FIELD = 0x1,
};

#define VM_INSTR_SET(c, errlbl, ec) do {                                \
        unsigned int instr;                                             \
        instr = VM_INSTR(VM_OP_SET, VM_SET_NONE);                       \
        ec = vm_chunk_add(c, &instr, 1, sizeof(instr));                 \
        if (ec)                                                         \
            goto errlbl;                                                \
    } while (0)

#define VM_INSTR_SET_FIELD(c, errlbl, ec) do {                          \
        unsigned int instr;                                             \
        instr = VM_INSTR(VM_OP_SET, VM_SET_FIELD);                      \
        ec = vm_chunk_add(c, &instr, 1, sizeof(instr));                 \
    } while (0)


/*
 * GET instruction
 */

enum {
    VM_GET_NONE  = 0x0,
    VM_GET_FIELD = 0x1,
};

#define VM_INSTR_GET_FIELD(c, errlbl, ec) do {                          \
        unsigned int instr;                                             \
        instr = VM_INSTR(VM_OP_GET, VM_GET_FIELD);                      \
        ec = vm_chunk_add(c, &instr, 1, sizeof(instr));                 \
    } while (0)



/*
 * CALL instructions
 */

#define VM_INSTR_CALL(c, errlbl, ec, narg) do {                         \
        unsigned int instr;                                             \
        instr = VM_INSTR(VM_OP_CALL, narg);                             \
        ec    = vm_chunk_add(c, &instr, 1, sizeof(instr));              \
    } while (0)



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
 * VM function calls
 */

typedef int (*vm_action_t)(void *data, char *name,
                           vm_stack_entry_t *args, int narg,
                           vm_stack_entry_t *retval);

typedef struct vm_method_s {
    char        *name;                       /* function name */
    int          id;                         /* function ID */
    vm_action_t  handler;                    /* function handler */
    void        *data;                       /* opaque user data */
} vm_method_t;


/*
 * VM state
 */

typedef struct vm_state_s {
    vm_stack_t    *stack;                     /* VM stack */
    vm_chunk_t    *chunk;                     /* code being executed */
    
    unsigned int  *pc;                        /* program counter */
    int            ninstr;                    /* # of instructions left */
    int            nsize;                     /* of code left */

    vm_method_t   *methods;                   /* action handlers */
    int            nmethod;
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
int vm_push       (vm_stack_t *s, int type, vm_value_t value);
int vm_push_int   (vm_stack_t *s, int i);
int vm_push_double(vm_stack_t *s, double d);
int vm_push_string(vm_stack_t *s, char *str);
int vm_push_global(vm_stack_t *s, vm_global_t *g);

vm_stack_entry_t *vm_args(vm_stack_t *s, int narg);

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
int          vm_global_lookup(char *name, vm_global_t **gp);
vm_global_t *vm_global_name  (char *name);
vm_global_t *vm_global_alloc (int nfact);

void         vm_global_free  (vm_global_t *g);
void         vm_global_print (vm_global_t *g);


GSList      *vm_fact_lookup(char *name);
void         vm_fact_reset (OhmFact *fact);
OhmFact     *vm_fact_dup   (OhmFact *src, char *name);
OhmFact     *vm_fact_copy  (OhmFact *dst, OhmFact *src);
void         vm_fact_remove(char *name);

int          vm_fact_set_field  (vm_state_t *vm, OhmFact *fact, char *field,
                                 int type, vm_value_t *value);
int          vm_fact_get_field  (vm_state_t *vm, OhmFact *fact, char *field,
                                 vm_value_t *value);
int          vm_fact_match_field(vm_state_t *vm, OhmFact *fact, char *field,
                                 GValue *gval, int type, vm_value_t *value);

void vm_fact_print(OhmFact *fact);

/* vm-method.c */
int          vm_method_add    (vm_state_t *vm,
                               char *name, vm_action_t handler, void *data);
vm_method_t *vm_method_lookup (vm_state_t *vm, char *name);
vm_method_t *vm_method_by_id  (vm_state_t *vm, int id);
vm_action_t  vm_method_default(vm_state_t *vm, vm_action_t handler);
int          vm_method_call   (vm_state_t *vm,
                               char *name, vm_method_t *m, int narg);

/* vm.c */
int vm_init(vm_state_t *vm, int stack_size);
int vm_exec(vm_state_t *vm, vm_chunk_t *code);


#endif /* __DRES_VM_H__ */



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
