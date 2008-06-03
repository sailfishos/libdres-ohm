#ifndef __POLICY_DRES_H__
#define __POLICY_DRES_H__

#include <glib.h>
#include <dres/variables.h>

enum {
    DRES_TYPE_UNKNOWN = 0,
    DRES_TYPE_TARGET,
    DRES_TYPE_FACTVAR,
    DRES_TYPE_DRESVAR,
    DRES_TYPE_LITERAL,
    DRES_TYPE_DELETED   = 0x40,
    DRES_TYPE_UNDEFINED = 0x80
};

#define DRES_ID_NONE -1

#define DRES_TYPE(type)     ((DRES_TYPE_##type) << 24)
#define DRES_LITERAL(value) (DRES_TYPE(LITERAL)  | (value))
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

typedef struct dres_scope_s   dres_scope_t;
typedef struct dres_handler_s dres_handler_t;

struct dres_s;
typedef struct dres_s dres_t;

typedef struct {
    int *ids;                              /* prerequisite IDs */
    int  nid;                              /* number of prerequisites */
} dres_prereq_t;

typedef struct dres_action_s dres_action_t;

typedef struct {
    int   variable;                        /* variable ID */
    char *selector;                        /* selector or NULL */
    char *field;                           /* field or NULL */
} dres_varref_t;


enum {
    DRES_ASSIGN_IMMEDIATE = 0,
    DRES_ASSIGN_VARIABLE,
};


typedef struct {
#if 1
    dres_varref_t lvalue;                  /* variable to assign to */
    int           type;                    /* DRES_ASSIGN_* */
    union {
        dres_varref_t var;                 /* variable */
        int           val;                 /* value */
    };
#else
    int           var_id;                  /* variable ID */
    int           val_id;                  /* value ID */
#endif
} dres_assign_t;



#define DRES_BUILTIN_ASSIGN  "__assign"
#define DRES_BUILTIN_UNKNOWN "__unknown"
struct dres_action_s {
    char           *name;                  /* name(...) */
    dres_varref_t   lvalue;                /* variable to put the result to */
    dres_varref_t   rvalue;                /* variable to copy if any, or */
    int             immediate;             /* immediate value XXX kludge */
    dres_handler_t *handler;               /* handler */
    int            *arguments;             /* name(arguments...) */
    int             nargument;             /* number of arguments */
    dres_assign_t  *variables;             /* name(arguments, variables) */
    int             nvariable;             /* number of variables */
    dres_action_t  *next;                  /* more actions */
};

struct dres_handler_s {
    char  *name;                               /* action name */
    int  (*handler)(dres_t *dres, char *name,  /* action handler */
                    dres_action_t *action, void **ret); 
};

typedef union {
    char         *string;
    dres_array_t *array;
} dres_val_t;

typedef struct {
    int          id;
    int          stamp;                     /* last update stamp */
    char        *name;
    dres_var_t  *var;
    dres_val_t   val;
} dres_variable_t;

typedef struct {
    int   id;
    char *name;
} dres_literal_t;

typedef struct {
    int            id;                      /* target ID */
    char          *name;                    /* target name */
    dres_prereq_t *prereqs;                 /* prerequisites */
    dres_action_t *actions;                 /* associated actions */
    int            stamp;                   /* last update stamp */
} dres_target_t;

typedef struct {
    int            ntarget;
    int            nfactvar;
    int            ndresvar;
    dres_prereq_t *depends;                 /* reversed prerequisites */
} dres_graph_t;

enum {
    DRES_FLAG_UNKNOWN      = 0x0,
    DRES_ACTIONS_FINALIZED = 0x1,           /* actions resolved to handlers */
};

#define DRES_TST_FLAG(d, f) ((d)->flags &   DRES_##f)
#define DRES_SET_FLAG(d, f) ((d)->flags |=  DRES_##f)
#define DRES_CLR_FLAG(d, f) ((d)->flags &= ~DRES_##f)


#define DRES_VAR_FIELD "value"              /* field name for variables */

struct dres_scope_s {
    dres_store_t *curr;                     /* current variables */
    GHashTable   *names;                    /* names of current variables */
    dres_scope_t *prev;                     /* previous scope */
};

struct dres_s {
    dres_target_t   *targets;
    int              ntarget;
    dres_variable_t *factvars;
    int              nfactvar;
    dres_variable_t *dresvars;
    int              ndresvar;
    dres_literal_t  *literals;
    int              nliteral;

    int              stamp;

    dres_store_t    *fact_store;
    dres_store_t    *dres_store;

    dres_scope_t    *scope;

    dres_handler_t  *handlers;
    int              nhandler;
    dres_handler_t   fallback;

    unsigned long    flags;
};


extern int depth;


#ifndef TRUE
#    define FALSE 0
#    define TRUE  1
#endif

#define ALLOC(type) ({                            \
            type   *__ptr;                        \
            size_t  __size = sizeof(type);        \
                                                  \
            if ((__ptr = malloc(__size)) != NULL) \
                memset(__ptr, 0, __size);         \
            __ptr; })

#define ALLOC_OBJ(ptr) ((ptr) = ALLOC(typeof(*ptr)))

#define ALLOC_ARR(type, n) ({                     \
            type   *__ptr;                        \
            size_t   __size = (n) * sizeof(type); \
                                                  \
            if ((__ptr = malloc(__size)) != NULL) \
                memset(__ptr, 0, __size);         \
            __ptr; })

#define REALLOC_ARR(ptr, o, n) ({                                       \
            typeof(ptr) __ptr;                                          \
            size_t      __size = sizeof(*ptr) * (n);                    \
                                                                        \
            if ((ptr) == NULL) {                                        \
                (__ptr) = ALLOC_ARR(typeof(*ptr), n);                   \
                ptr = __ptr;                                            \
            }                                                           \
            else if ((__ptr = realloc(ptr, __size)) != NULL) {          \
                if ((n) > (o))                                          \
                    memset(__ptr + (o), 0, ((n)-(o)) * sizeof(*ptr));   \
                ptr = __ptr;                                            \
            }                                                           \
            __ptr; })
                
#define FREE(obj) do { if (obj) free(obj); } while (0)

#define STRDUP(s) ({                                    \
            char *__s = s;                              \
            __s = ((s) ? strdup(s) : strdup(""));       \
            __s; })

#define DEBUG(fmt, args...) do {                                        \
        if (depth > 0)                                                  \
            printf("%*.*s ", depth*2, depth*2, "                  ");   \
        printf("[%s] "fmt"\n", __FUNCTION__, ## args);                  \
    } while (0)



/* dres.c */
dres_t *dres_init(char *prefix);
void    dres_exit(dres_t *dres);
int     dres_parse_file(dres_t *dres, char *path);
int     dres_set_prefix(dres_t *dres, char *prefix);
char *  dres_get_prefix(dres_t *dres);

dres_variable_t *dres_lookup_variable(dres_t *dres, int id);




/* target.c */
int            dres_add_target   (dres_t *dres, char *name);
int            dres_target_id    (dres_t *dres, char *name);
dres_target_t *dres_lookup_target(dres_t *dres, char *name);
void           dres_free_targets (dres_t *dres);
void           dres_dump_targets (dres_t *dres);
int            dres_check_target (dres_t *dres, int tid);

/* factvar.c */
int  dres_add_factvar  (dres_t *dres, char *name);
int  dres_factvar_id   (dres_t *dres, char *name);
void dres_free_factvars(dres_t *dres);
int  dres_check_factvar(dres_t *dres, int id, int stamp);

/* dresvar.c */
int  dres_add_dresvar  (dres_t *dres, char *name);
int  dres_dresvar_id   (dres_t *dres, char *name);
void dres_free_dresvars(dres_t *dres);
int  dres_check_dresvar(dres_t *dres, int id, int stamp);

/* literal.c */
int  dres_add_literal  (dres_t *dres, char *name);
int  dres_literal_id   (dres_t *dres, char *name);
void dres_free_literals(dres_t *dres);

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

/* builtin.c */
int dres_register_builtins(dres_t *dres);

/* scope.c */
int   dres_scope_setvar   (dres_scope_t *scope, char *name, char *value);
char *dres_scope_getvar   (dres_scope_t *scope, char *name);
int   dres_scope_push_args(dres_t *dres, char **args);


#if 1
int dres_add_assignment(dres_action_t *action, dres_assign_t *assignemnt);
#else
int dres_add_assignment(dres_action_t *action, int var, int val);
#endif

dres_graph_t *dres_build_graph(dres_t *dres, char *goal);
void          dres_free_graph (dres_graph_t *graph);

char *dres_name(dres_t *, int id, char *buf, size_t bufsize);
char *dres_dump_varref(dres_t *dres, char *buf, size_t bufsize,
                       dres_varref_t *vr);
int  *dres_sort_graph(dres_t *dres, dres_graph_t *graph);
void  dres_dump_sort(dres_t *dres, int *list);

int dres_update_goal(dres_t *dres, char *goal, char **locals);

dres_handler_t *dres_lookup_handler(dres_t *dres, char *name);

int dres_register_handler(dres_t *dres, char *name,
                          int (*)(dres_t *, char *, dres_action_t *, void **));
int dres_fallback_handler(dres_t *dres,
                          int (*handler)(dres_t *,
                                         char *, dres_action_t *, void **));

int dres_run_actions(dres_t *dres, dres_target_t *target);



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


#endif /* __DEPENDENCY_H__ */
