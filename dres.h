#ifndef __POLICY_DRES_H__
#define __POLICY_DRES_H__

#include "variables.h"

enum {
    DRES_TYPE_UNKNOWN = 0,
    DRES_TYPE_TARGET,
    DRES_TYPE_VARIABLE,
    DRES_TYPE_LITERAL,
    DRES_TYPE_DELETED   = 0x40,
    DRES_TYPE_UNDEFINED = 0x80
};

#define DRES_ID_NONE -1

#define DRES_TYPE(type)     ((DRES_TYPE_##type) << 24)
#define DRES_LITERAL(value) (DRES_TYPE(LITERAL)  | (value))
#define DRES_VARIABLE(id)   (DRES_TYPE(VARIABLE) | (id))
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


typedef struct {
    int *ids;                              /* prerequisite IDs */
    int  nid;                              /* number of prerequisites */
} dres_prereq_t;

typedef struct dres_action_s dres_action_t;

typedef struct {
    int           var_id;                  /* variable ID */
    int           val_id;                  /* value ID */
} dres_assign_t;

struct dres_action_s {
    char          *name;                   /* name(...) */
    int            lvalue;                 /* variable to put the result to */
    int           *arguments;              /* name(arguments...) */
    int            nargument;              /* number of arguments */
    dres_assign_t *variables;              /* name(arguments, variables) */
    int            nvariable;              /* number of variables */
    dres_action_t *next;                   /* more actions */
};

typedef union {
    char         *string;
    dres_array_t *array;
} dres_val_t;


typedef struct {
    int          id;
    char        *name;
    int          stamp;                /* last update stamp */
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
    int            nvariable;
    dres_prereq_t *depends;                 /* reversed prerequisites */
} dres_graph_t;


typedef struct {
    dres_target_t   *targets;
    int              ntarget;
#if 1
    dres_variable_t *variables;
    int              nvariable;
#else
    dres_variable_t *factvars;
    int              nfactvar;
    dres_variable_t *dresvars;
    int              ndresvar;
#endif
    dres_literal_t  *literals;
    int              nliteral;

    int              stamp;

    dres_store_t    *fact_store;
    dres_store_t    *dres_store;
} dres_t;




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





dres_t *dres_init(char *rulefile);
void    dres_exit(dres_t *dres);

int dres_variable_id(dres_t *dres, char *name);
int dres_literal_id (dres_t *dres, char *name);
int dres_target_id  (dres_t *dres, char *name);

dres_target_t *dres_lookup_target(dres_t *dres, char *name);

dres_prereq_t *dres_new_prereq(int id);
int            dres_add_prereq(dres_prereq_t *dep, int id);

dres_action_t *dres_new_action  (int argument);
void           dres_free_actions(dres_action_t *action);
#define dres_free_action dres_free_actions
int            dres_add_argument(dres_action_t *action, int argument);
void           dres_dump_action(dres_t *dres, dres_action_t *a);

int dres_add_assignment(dres_action_t *action, int var, int val);


void dres_dump_targets(dres_t *dres);

dres_graph_t *dres_build_graph(dres_t *dres, char *goal);
void          dres_free_graph (dres_graph_t *graph);

char *dres_name(dres_t *, int id, char *buf, size_t bufsize);
int  *dres_sort_graph(dres_t *dres, dres_graph_t *graph);
void  dres_dump_sort(dres_t *dres, int *list);

int dres_update_goal(dres_t *dres, char *goal);


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


#endif /* __DEPENDENCY_H__ */
