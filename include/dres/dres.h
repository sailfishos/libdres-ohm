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


#ifndef __POLICY_DRES_H__
#define __POLICY_DRES_H__

#include <stdarg.h>
#include <glib.h>

#include <dres/vm.h>
#include <dres/mm.h>

#define DRES_ERROR   VM_ERROR
#define DRES_WARNING VM_WARNING
#define DRES_INFO    VM_INFO

#define DRES_LOG_FATAL   VM_LOG_FATAL
#define DRES_LOG_ERROR   VM_LOG_ERROR
#define DRES_LOG_WARNING VM_LOG_WARNING
#define DRES_LOG_NOTICE  VM_LOG_NOTICE
#define DRES_LOG_INFO    VM_LOG_INFO


#define DRES_MAGIC    ('D'<<24|('R'<<16)|('E'<<8)|'S')
#define DRES_MAX_NAME 128

#define DRES_SUFFIX_BINARY "dresc"
#define DRES_SUFFIX_PLAIN  "dres"


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

#define DRES_ACTION_SUCCEED    return TRUE
#define DRES_ACTION_FAIL       return FALSE
#define DRES_ACTION_ERROR(err) do { return (err) > 0 ? -(err):(err); } while (0)

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


typedef enum {
    DRES_OP_UNKNOWN = 0,
    DRES_OP_EQ  = VM_RELOP_EQ,
    DRES_OP_NEQ = VM_RELOP_NE,
} dres_op_t;


typedef struct dres_select_s dres_select_t;    /* rename to dres_selector_t */
struct dres_select_s {
    dres_field_t   field;
    dres_select_t *next;
    dres_op_t      op;
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


typedef union dres_expr_u dres_expr_t;
typedef enum {
    DRES_RELOP_UNKNOWN = VM_RELOP_UNKNOWN,
    DRES_RELOP_EQ      = VM_RELOP_EQ,
    DRES_RELOP_NE      = VM_RELOP_NE,
    DRES_RELOP_LT      = VM_RELOP_LT,
    DRES_RELOP_LE      = VM_RELOP_LE,
    DRES_RELOP_GT      = VM_RELOP_GT,
    DRES_RELOP_GE      = VM_RELOP_GE,
    DRES_RELOP_NOT     = VM_RELOP_NOT,
    DRES_RELOP_OR      = VM_RELOP_OR,
    DRES_RELOP_AND     = VM_RELOP_AND
} dres_relop_t;

typedef enum {
    DRES_EXPR_UNKNOWN = 0,
    DRES_EXPR_CONST,
    DRES_EXPR_VARREF,
    DRES_EXPR_RELOP,
    DRES_EXPR_CALL
} dres_expr_type_t;


#define DRES_EXPR_COMMON                        \
    dres_expr_type_t  type;                     \
    dres_expr_t      *next

typedef struct {
    DRES_EXPR_COMMON;
    dres_relop_t  op;
    dres_expr_t  *arg1;
    dres_expr_t  *arg2;
} dres_expr_relop_t;

typedef struct {
    DRES_EXPR_COMMON;
    dres_varref_t ref;
} dres_expr_varref_t;

typedef struct {
    DRES_EXPR_COMMON;
    int vtype;
    union {
        int     i;
        char   *s;
        double  d;
    } v;
} dres_expr_const_t;

typedef struct {
    DRES_EXPR_COMMON;
    char             *name;
    dres_handler_t    handler;
    dres_expr_t      *args;
    dres_local_t     *locals;
} dres_expr_call_t;

typedef struct {
    DRES_EXPR_COMMON;
} dres_expr_any_t;

union dres_expr_u {
    dres_expr_type_t   type;
    dres_expr_any_t    any;
    dres_expr_const_t  constant;
    dres_expr_varref_t varref;
    dres_expr_relop_t  relop;
    dres_expr_call_t   call;
};


union dres_stmt_u;
typedef union dres_stmt_u dres_stmt_t;

typedef enum {
    DRES_STMT_UNKNOWN = 0,
    DRES_STMT_FULL_ASSIGN,
    DRES_STMT_PARTIAL_ASSIGN,
    DRES_STMT_CALL,
    DRES_STMT_IFTHEN,
} dres_stmt_type_t;



#define DRES_STMT_COMMON                        \
    dres_stmt_type_t  type;                     \
    dres_stmt_t      *next

typedef struct {
    DRES_STMT_COMMON;
    dres_expr_t      *condition;
    dres_stmt_t      *if_branch;
    dres_stmt_t      *else_branch;
} dres_stmt_if_t;

typedef struct {
    DRES_STMT_COMMON;
    dres_expr_varref_t *lvalue;
    dres_expr_t        *rvalue;
} dres_stmt_assign_t;

typedef struct {
    DRES_STMT_COMMON;
    char             *name;
    dres_handler_t    handler;
    dres_expr_t      *args;
    dres_local_t     *locals;
} dres_stmt_call_t;

typedef struct {
    DRES_STMT_COMMON;
} dres_stmt_any_t;

union dres_stmt_u {
    dres_stmt_type_t   type;
    dres_stmt_any_t    any;
    dres_stmt_if_t     ifthen;
    dres_stmt_assign_t assign;
    dres_stmt_call_t   call;
};


enum {
    DRES_ACTION_VALUE = 0,                 /* assignment of basic type */
    DRES_ACTION_VARREF,                    /* assignment of variable */
    DRES_ACTION_CALL                       /* assignment of method call */
};


enum {
    DRES_ASSIGN_FULL   = 0,               /* default (full) assignmnent */
    DRES_ASIGN_DEFAULT = DRES_ASSIGN_FULL,
    DRES_ASSIGN_PARTIAL                    /* partial assignment */
};


#define DRES_BUILTIN_UNKNOWN "__unknown"

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
    dres_stmt_t   *statements;              /* associated actions */
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
    DRES_COMPILED           = 0x8,          /* compiled dres buffer */
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


typedef struct {
    u_int32_t magic;                               /* DRES_MAGIC */
    u_int32_t ssize;                               /* string table size */
    u_int32_t ntarget;                             /* # of targets */
    u_int32_t nprereq;                             /* # of prereqs */
    u_int32_t ncode;                               /* # of VM code chunks */
    u_int32_t sinstr;                              /* size of VM instructions */
    u_int32_t ndependency;                         /* # of dependencies */
    u_int32_t nvariable;                           /* # of variables */
    u_int32_t ninit;                               /* # of initializers */
    u_int32_t nfield;                              /* # of fields */
    u_int32_t nmethod;                             /* # of methods */
} dres_header_t;

typedef struct {
    dres_header_t header;
    int           error;
    char         *data;
    u_int32_t     dsize;
    u_int32_t     dused;
    char         *strings;
    u_int32_t     ssize;
    u_int32_t     sused;
    int           fd;
} dres_buf_t;

#define DRES_RELOCATE(ptr, diff) ((ptr) = ((void *)(ptr)) + (diff))

#define DRES_ALIGN_TO   VM_ALIGN_TO
#define DRES_ALIGNED    VM_ALIGNED
#define DRES_ALIGNMENT  VM_ALIGNMENT
#define DRES_ALIGNED_OK VM_ALIGNED_OK

extern int depth;

#ifndef TRUE
#    define FALSE 0
#    define TRUE  1
#endif


/* dres.c */
dres_t *dres_open(char *path);
#define dres_close dres_exit

dres_t *dres_init(char *prefix);
void    dres_exit(dres_t *dres);
dres_t *dres_parse_file(char *path);
int     dres_finalize(dres_t *dres);

dres_variable_t *dres_lookup_variable(dres_t *dres, int id);
void dres_update_var_stamp(dres_t *dres, dres_variable_t *var);
void dres_update_target_stamp(dres_t *dres, dres_target_t *target);

int     dres_save(dres_t *dres, char *path);
dres_t *dres_load(char *path);
void    dres_free_value(dres_value_t *val);
void    dres_free_field(dres_field_t *f);

typedef vm_log_level_t dres_log_level_t;
void dres_set_logger(void (*logger)(dres_log_level_t, const char *, va_list));

/* target.c */
int            dres_add_target   (dres_t *dres, char *name);
int            dres_target_id    (dres_t *dres, char *name);
dres_target_t *dres_lookup_target(dres_t *dres, char *name);
void           dres_free_targets (dres_t *dres);
void           dres_dump_targets (dres_t *dres);
int            dres_check_target (dres_t *dres, int tid);
int            dres_save_targets (dres_t *dres, dres_buf_t *buf);
int            dres_load_targets (dres_t *dres, dres_buf_t *buf);


/* factvar.c */
int         dres_add_factvar  (dres_t *dres, char *name);
int         dres_factvar_id   (dres_t *dres, char *name);
const char *dres_factvar_name (dres_t *dres, int id);
void        dres_free_factvars(dres_t *dres);
int         dres_check_factvar(dres_t *dres, int id, int stamp);
void        dres_dump_init    (dres_t *dres);
int         dres_save_factvars(dres_t *dres, dres_buf_t *buf);
int         dres_load_factvars(dres_t *dres, dres_buf_t *buf);


/* dresvar.c */
int         dres_add_dresvar  (dres_t *dres, char *name);
int         dres_dresvar_id   (dres_t *dres, char *name);
const char *dres_dresvar_name (dres_t *dres, int id);

void dres_free_dresvars(dres_t *dres);
int  dres_check_dresvar(dres_t *dres, int id, int stamp);

int  dres_local_value(dres_t *dres, int id, dres_value_t *value);

int  dres_save_dresvars(dres_t *dres, dres_buf_t *buf);
int  dres_load_dresvars(dres_t *dres, dres_buf_t *buf);



/* prereq.c */
dres_prereq_t *dres_new_prereq (int id);
int            dres_add_prereq (dres_prereq_t *dep, int id);
void           dres_free_prereq(dres_prereq_t *dep);

/* action.c */
void           dres_free_locals(dres_local_t *locals);
void           dres_free_varref(dres_varref_t *vref);

void          dres_free_value (dres_value_t *value);
int           dres_print_value(dres_t *dres,
                               dres_value_t *value, char *buf, size_t size);
int           dres_print_locals(dres_t *dres, dres_local_t *locals,
                                char *buf, size_t size);
int           dres_print_varref(dres_t *dres, dres_varref_t *vr,
                                char *buf, size_t size);


/* builtin.c */
int dres_register_builtins(dres_t *dres);
dres_handler_t dres_fallback_handler(dres_t *dres, dres_handler_t handler);


/* ast.c */
void dres_dump_statement(dres_t *dres, dres_stmt_t *stmt, int level);
void dres_free_statement(dres_stmt_t *stmt);
void dres_free_expr(dres_expr_t *expr);


/* compiler.c */
int dres_compile_target(dres_t *dres, dres_target_t *target);

dres_buf_t *dres_buf_create (int dsize, int ssize);
void        dres_buf_destroy(dres_buf_t *buf);
void *dres_buf_alloc(dres_buf_t *buf, size_t size);
char *dres_buf_stralloc(dres_buf_t *buf, char *str);

int dres_buf_wu16(dres_buf_t *buf, u_int16_t i);
int dres_buf_ws16(dres_buf_t *buf, int16_t i);
int dres_buf_wu32(dres_buf_t *buf, u_int32_t i);
int dres_buf_ws32(dres_buf_t *buf, int32_t i);
int dres_buf_wstr(dres_buf_t *buf, char *str);
int dres_buf_wbuf(dres_buf_t *buf, char *data, int size);

int dres_buf_wdbl(dres_buf_t *buf, double d);
u_int32_t dres_buf_ru32(dres_buf_t *buf);
int32_t   dres_buf_rs32(dres_buf_t *buf);
char     *dres_buf_rstr(dres_buf_t *buf);
char     *dres_buf_rbuf(dres_buf_t *buf, int size);
double    dres_buf_rdbl(dres_buf_t *buf);

int     dres_save(dres_t *dres, char *path);
dres_t *dres_load(char *path);


dres_graph_t *dres_build_graph(dres_t *dres, dres_target_t *goal);
void          dres_free_graph (dres_graph_t *graph);

char *dres_name(dres_t *, int id, char *buf, size_t bufsize);
int   dres_print_varref(dres_t *dres, dres_varref_t *v, char *buf, size_t size);
int  *dres_sort_graph(dres_t *dres, dres_graph_t *graph);
void  dres_dump_sort(dres_t *dres, int *list);

int dres_update_goal(dres_t *dres, char *goal, char **locals);

dres_handler_t dres_lookup_handler(dres_t *dres, char *name);

int dres_register_handler(dres_t *dres, char *name, dres_handler_t handler);
int dres_unregister_handler(dres_t *dres, char *name, dres_handler_t handler);

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
