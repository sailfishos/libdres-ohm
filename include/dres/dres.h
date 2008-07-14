#ifndef __POLICY_DRES_H__
#define __POLICY_DRES_H__

#include <glib.h>
#include <dres/vm.h>
#include <dres/mm.h>

#define DRES_MAX_NAME 128

enum {
    DRES_TYPE_UNKNOWN   = VM_TYPE_UNKNOWN,
    DRES_TYPE_NIL       = VM_TYPE_NIL,
    DRES_TYPE_INTEGER   = VM_TYPE_INTEGER,
    DRES_TYPE_DOUBLE    = VM_TYPE_DOUBLE,
    DRES_TYPE_STRING    = VM_TYPE_STRING,
    DRES_TYPE_DRESVAR   = VM_TYPE_LOCAL,
    DRES_TYPE_FACTVAR   = VM_TYPE_GLOBAL,
    DRES_TYPE_TARGET,
    DRES_TYPE_DELETED   = 0x40,
    DRES_TYPE_UNDEFINED = 0x80
};

#define DRES_ID_NONE -1

#define DRES_TYPE(type)     ((DRES_TYPE_##type) << 24)
#define DRES_FACTVAR(id)    (DRES_TYPE(FACTVAR)  | (id))
#define DRES_DRESVAR(id)    (DRES_TYPE(DRESVAR)  | (id))
#define DRES_TARGET(id)     (DRES_TYPE(TARGET)   | (id))
#define DRES_UNDEFINED(id)  (DRES_TYPE(UNDEFINED) | (id))
#define DRES_DEFINED(id)    ((id) & ~DRES_TYPE(UNDEFINED))
#define DRES_IS_DEFINED(id) (!(id & DRES_TYPE(UNDEFINED)))
#define DRES_VALUE_MASK     0x00ffffff
#define DRES_TYPE_MASK      ((~DRES_TYPE(UNDEFINED)&~DRES_TYPE(DELETED)) & \
                             ~DRES_VALUE_MASK)
#define DRES_ID_TYPE(id)    ((id & DRES_TYPE_MASK) >> 24)
#define DRES_IS(id, type)   (DRES_ID_TYPE(id) == (type))
#define DRES_INDEX(id)      ((id) & 0x00ffffff)
#define DRES_DELETED(id)    ((id) | DRES_TYPE(DELETED))
#define DRES_IS_DELETED(id) ((id) & DRES_TYPE(DELETED))


#define DRES_ACTION(b)                                                  \
    static int b(void *data, char *name,                                \
                 vm_stack_entry_t *args, int narg, vm_stack_entry_t *rv)


typedef vm_action_t dres_handler_t;

struct dres_s;
typedef struct dres_s dres_t;

typedef struct {
    int *ids;                              /* prerequisite IDs */
    int  nid;                              /* number of prerequisites */
} dres_prereq_t;

typedef struct dres_varref_s dres_varref_t;

typedef struct {
    int type;
    union {
        int     i;                         /* DRES_TYPE_INTEGER */
        double  d;                         /* DRES_TYPE_DOUBLE */
        char   *s;                         /* DRES_TYPE_STRING */
        int     id;                        /* DRES_TYPE_*VAR */
#if 0
        dres_varref_t *var;                /* DRES_TYPE_FACTVAR */
#endif
    } v;
} dres_value_t;

typedef struct {
    char         *name;
    dres_value_t  value;
} dres_field_t;


typedef struct dres_select_s dres_select_t;    /* rename to dres_selector_t */
struct dres_select_s {
    dres_field_t   field;
    dres_select_t *next;
};

typedef struct dres_init_s dres_init_t;
struct dres_init_s {
    dres_field_t  field;
    dres_init_t  *next;
};

typedef struct dres_initializer_s dres_initializer_t;
struct dres_initializer_s {
    int                 variable;          /* variable ID */
    dres_init_t        *fields;
    dres_initializer_t *next;
};

typedef struct dres_arg_s dres_arg_t;
struct dres_arg_s {
    dres_value_t  value;
    dres_arg_t   *next;
};


typedef struct dres_local_s dres_local_t;
struct dres_local_s {
    int           id;
    dres_value_t  value;
    dres_local_t *next;
};


struct dres_varref_s {
    int            variable;               /* $var */
    dres_select_t *selector;               /* [ field1:value1, ... ] */
    char          *field;                  /* :field */
};


typedef struct {
    char           *name;                  /* method name */
    dres_handler_t  handler;               /* method handler */
    dres_arg_t     *args;                  /* arguments passed by value */
    dres_local_t   *locals;                /* arguments passed by name */
} dres_call_t;

enum {
    DRES_ACTION_VALUE = 0,                 /* assignment of basic type */
    DRES_ACTION_VARREF,                    /* assignment of variable */
    DRES_ACTION_CALL                       /* assignment of method call */
};


#define DRES_BUILTIN_UNKNOWN "__unknown"
#define DRES_BUILTIN_ASSIGN  "__assign"

typedef struct dres_action_s dres_action_t;

struct dres_action_s {
    dres_varref_t      lvalue;             /* result variable if any */
    int                type;               /* DRES_ACTION_* */
    union {
        dres_value_t   value;
        dres_varref_t  rvalue;
        dres_call_t   *call;
    };
    dres_action_t     *next;
};


typedef struct {
    int   id;                               /* variable ID */
    int   stamp;                            /* last update stamp */
    int   txid;                             /*   of stamp */
    int   txstamp;                          /* stamp before txid */
    char *name;                             /* variable name */
    int   flags;                            /* DRES_VAR_* */
} dres_variable_t;

enum {
    DRES_VAR_UNKNOWN = 0x0,
    DRES_VAR_PREREQ  = 0x1
};


typedef struct {
    int            id;                      /* target ID */
    char          *name;                    /* target name */
    dres_prereq_t *prereqs;                 /* prerequisites */
    dres_action_t *actions;                 /* associated actions */
    vm_chunk_t    *code;                    /* VM code */
    int            stamp;                   /* last update stamp */
    int            txid;                    /* of stamp */
    int            txstamp;                 /* stamp before txid */
    int           *dependencies;            /* sorted depedencies */
} dres_target_t;

typedef struct {
    int            ntarget;
    int            nfactvar;
    int            ndresvar;
    dres_prereq_t *depends;                 /* reversed prerequisites */
} dres_graph_t;


typedef struct dres_store_s {
    OhmFactStore     *fs;                   /* fact store of our globals */
    OhmFactStoreView *view;                 /* to track our globals */
    GHashTable       *ht;                   /* hash table of our globals */
} dres_store_t;


enum {
    DRES_FLAG_UNKNOWN       = 0x0,
    DRES_ACTIONS_FINALIZED  = 0x1,          /* actions resolved to handlers */
    DRES_TARGETS_FINALIZED  = 0x2,          /* sorted dependency graph */
    DRES_TRANSACTION_ACTIVE = 0x4,          /* has an active transaction */
};

#define DRES_TST_FLAG(d, f) ((d)->flags &   DRES_##f)
#define DRES_SET_FLAG(d, f) ((d)->flags |=  DRES_##f)
#define DRES_CLR_FLAG(d, f) ((d)->flags &= ~DRES_##f)


struct dres_s {
    dres_target_t   *targets;
    int              ntarget;
    dres_variable_t *factvars;
    int              nfactvar;
    dres_variable_t *dresvars;
    int              ndresvar;
    dres_store_t     store;
    
    int              stamp;
    int              txid;                  /* transaction id */

    dres_handler_t   fallback;
    unsigned long    flags;
    
    dres_initializer_t *initializers;
    
    vm_state_t         vm;
};


extern int depth;

#ifndef TRUE
#    define FALSE 0
#    define TRUE  1
#endif


/* dres.c */
dres_t *dres_init(char *prefix);
void    dres_exit(dres_t *dres);
int     dres_parse_file(dres_t *dres, char *path);
int     dres_finalize(dres_t *dres);

dres_variable_t *dres_lookup_variable(dres_t *dres, int id);
void dres_update_var_stamp(void *dresp, void *varp);
void dres_update_target_stamp(dres_t *dres, dres_target_t *target);



/* target.c */
int            dres_add_target   (dres_t *dres, char *name);
int            dres_target_id    (dres_t *dres, char *name);
dres_target_t *dres_lookup_target(dres_t *dres, char *name);
void           dres_free_targets (dres_t *dres);
void           dres_dump_targets (dres_t *dres);
int            dres_check_target (dres_t *dres, int tid);

/* factvar.c */
int         dres_add_factvar  (dres_t *dres, char *name);
int         dres_factvar_id   (dres_t *dres, char *name);
const char *dres_factvar_name (dres_t *dres, int id);
void        dres_free_factvars(dres_t *dres);
int         dres_check_factvar(dres_t *dres, int id, int stamp);
void        dres_dump_init    (dres_t *dres);

/* dresvar.c */
int         dres_add_dresvar  (dres_t *dres, char *name);
int         dres_dresvar_id   (dres_t *dres, char *name);
const char *dres_dresvar_name (dres_t *dres, int id);

void dres_free_dresvars(dres_t *dres);
int  dres_check_dresvar(dres_t *dres, int id, int stamp);

int  dres_local_value(dres_t *dres, int id, dres_value_t *value);

/* prereq.c */
dres_prereq_t *dres_new_prereq (int id);
int            dres_add_prereq (dres_prereq_t *dep, int id);
void           dres_free_prereq(dres_prereq_t *dep);

/* action.c */
dres_action_t *dres_new_action  (int argument);
void           dres_free_actions(dres_action_t *action);
int            dres_add_argument(dres_action_t *action, int argument);
void           dres_dump_action (dres_t *dres, dres_action_t *action);
#define        dres_free_action dres_free_actions
dres_call_t   *dres_new_call (char *name, dres_arg_t *args, dres_local_t *vars);
void           dres_free_call(dres_call_t *call);

dres_value_t *dres_copy_value (dres_value_t *value);
void          dres_free_value (dres_value_t *value);
int           dres_print_value(dres_t *dres,
                               dres_value_t *value, char *buf, size_t size);

/* builtin.c */
int dres_register_builtins(dres_t *dres);

/* compiler.c */
int dres_compile_target(dres_t *dres, dres_target_t *target);
int dres_compile_action(dres_t *dres, dres_action_t *action, vm_chunk_t *code);


dres_graph_t *dres_build_graph(dres_t *dres, dres_target_t *goal);
void          dres_free_graph (dres_graph_t *graph);

char *dres_name(dres_t *, int id, char *buf, size_t bufsize);
int   dres_print_varref(dres_t *dres, dres_varref_t *v, char *buf, size_t size);
int  *dres_sort_graph(dres_t *dres, dres_graph_t *graph);
void  dres_dump_sort(dres_t *dres, int *list);

int dres_update_goal(dres_t *dres, char *goal, char **locals);

dres_handler_t dres_lookup_handler(dres_t *dres, char *name);

int dres_register_handler(dres_t *dres, char *name, dres_handler_t handler);

int dres_run_actions(dres_t *dres, dres_target_t *target);


/* variables.c */
int  dres_store_init (dres_t *dres);
void dres_store_free (dres_t *dres);
int  dres_store_track(dres_t *dres);
int  dres_store_check(dres_t *dres);

int  dres_store_tx_new     (dres_t *dres);
int  dres_store_tx_commit  (dres_t *dres);
int  dres_store_tx_rollback(dres_t *dres);


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


#endif /* __POLICY_DRES_H__ */
