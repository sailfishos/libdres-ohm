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

typedef struct {
    int               id;
    char             *name;
    int               stamp;                /* last update stamp */
    dres_var_t       *var;
    union {
        char         *strval;
        dres_array_t *arrval;
    };
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



int  dres_init(char *rulefile);
void dres_exit(void);

int dres_variable_id(char *name);
int dres_literal_id (char *name);
int dres_target_id  (char *name);

dres_target_t *dres_lookup_target(char *name);

dres_prereq_t *dres_new_prereq(int id);
int            dres_add_prereq(dres_prereq_t *dep, int id);

dres_action_t *dres_new_action(int argument);
int            dres_add_argument(dres_action_t *action, int argument);
void           dres_dump_action(dres_action_t *a);

int dres_add_assignment(dres_action_t *action, int var, int val);


void dres_dump_targets(void);

dres_graph_t *dres_build_graph(char *goal);
void          dres_free_graph (dres_graph_t *graph);

char *dres_name(int id, char *buf, size_t bufsize);
int  *dres_sort_graph(dres_graph_t *graph);
void  dres_dump_sort(int *list);

int dres_update_goal(char *goal);


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


#endif /* __DEPENDENCY_H__ */
