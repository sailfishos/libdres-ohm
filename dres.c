#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "dres.h"
#include <vala/ohm-fact.h>

#undef STAMP_FORCED_UPDATE

#ifndef TRUE
#    define FALSE 0
#    define TRUE  1
#endif

#define DEBUG(fmt, args...) do {                                \
        printf("[%s] "fmt"\n", __FUNCTION__, ## args); \
    } while (0)

extern FILE *yyin;
extern int   yyparse(dres_t *dres);

static void free_targets(dres_t *dres);
static void free_variables(dres_t *dres);
static void free_literals(dres_t *dres);
static int  finalize_variables(dres_t *dres);


static void free_action (dres_action_t *action);
static void free_actions(dres_action_t *actions);

static void free_prereq(dres_prereq_t *dep);

static int graph_build_prereq(dres_t *dres, dres_graph_t *graph,
                              dres_target_t *target, int prereq);
static int graph_has_prereq(dres_graph_t *graph, int tid, int prid);
static int graph_add_prereq(dres_t *dres, dres_graph_t *graph,int tid,int prid);
static int graph_add_leafs(dres_t *dres, dres_graph_t *graph);

static int check_variable(dres_t *dres, int id, int stamp);
static int check_target  (dres_t *dres, int id);

static int execute_actions(dres_t *dres, dres_target_t *target);




/********************
 * dres_init
 ********************/
dres_t *
dres_init(char *path)
{
    dres_t *dres;
    int     status;

    if (path == NULL) {
        errno = EINVAL;
        return NULL;
    }
        
    if (ALLOC_OBJ(dres) == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    
    dres->fact_store = dres_store_init(STORE_FACT , "com.nokia.policy");
    dres->dres_store = dres_store_init(STORE_LOCAL, NULL);
    
    if (dres->fact_store == NULL || dres->dres_store == NULL)
        goto fail;

    if ((yyin = fopen(path, "r")) == NULL)
        goto fail;
    
    status = yyparse(dres);
    fclose(yyin);

    if (status)
        goto fail;

    dres->stamp = 1;

    if ((status = finalize_variables(dres)) != 0)
        goto fail;

    return dres;
    
 fail:
    dres_dump_targets(dres);
    dres_exit(dres);
    return NULL;
}


/********************
 * dres_exit
 ********************/
void
dres_exit(dres_t *dres)
{
    if (dres == NULL)
        return;
    
    free_targets(dres);
    free_variables(dres);
    free_literals(dres);
    
    dres_store_destroy(dres->fact_store);
    dres_store_destroy(dres->dres_store);

    FREE(dres);
}


/********************
 * dres_check_stores
 ********************/
void
dres_check_stores(dres_t *dres)
{
    dres_variable_t *var;
    int              i;
    char             name[128];

    for (i = 0, var = dres->variables; i < dres->nvariable; i++, var++) {
        sprintf(name, "com.nokia.policy.%s", var->name);
        if (!dres_store_check(dres->fact_store, name))
            DEBUG("*** lookup of %s FAILED", name);
    }
}


/*****************************************************************************
 *                            *** target handling ***                        *
 *****************************************************************************/

/********************
 * dres_add_target
 ********************/
int
dres_add_target(dres_t *dres, char *name)
{
    dres_target_t *target;
    int            id;


    if (REALLOC_ARR(dres->targets, dres->ntarget, dres->ntarget + 1) == NULL)
        return DRES_ID_NONE;

    id     = dres->ntarget++;
    target = dres->targets + id;
    
    target->id   = DRES_UNDEFINED(DRES_TARGET(id));
    target->name = STRDUP(name);

    return target->name ? target->id : DRES_ID_NONE;
}


/********************
 * dres_target_id
 ********************/
int
dres_target_id(dres_t *dres, char *name)
{
    dres_target_t *target;
    int            i;

    if (name != NULL)
        for (i = 0, target = dres->targets; i < dres->ntarget; i++, target++) {
            if (!strcmp(name, target->name))
                return target->id;
        }
    
    return dres_add_target(dres, name);
}


/********************
 * dres_lookup_target
 ********************/
dres_target_t *
dres_lookup_target(dres_t *dres, char *name)
{
    dres_target_t *target;
    int            i, id;
    
    for (i = 0, target = dres->targets; i < dres->ntarget; i++, target++)
        if (!strcmp(name, target->name))
            return target;
    
    if ((id = dres_target_id(dres, name)) == DRES_ID_NONE)
        return NULL;
    else
        return dres->targets + DRES_INDEX(id);
}


/********************
 * free_targets
 ********************/
void
free_targets(dres_t *dres)
{
    dres_target_t *target;
    int            i;

    for (i = 0, target = dres->targets; i < dres->ntarget; i++, target++) {
        FREE(target->name);
        FREE(target->prereqs);
        free_actions(target->actions);
    }

    FREE(dres->targets);
    dres->targets = NULL;
    dres->ntarget = 0;
}


/*****************************************************************************
 *                  *** prerequisite/dependency handling ***                 *
 *****************************************************************************/

/********************
 * dres_add_prereq
 ********************/
int
dres_add_prereq(dres_prereq_t *dep, int id)
{
    if (dep->nid < 0)                              /* unmark as not present */
        dep->nid = 0;

    if (REALLOC_ARR(dep->ids, dep->nid, dep->nid + 1) == NULL)
        return DRES_ID_NONE;

    dep->ids[dep->nid++] = id;

    return 0;
}


/********************
 * dres_new_prereq
 ********************/
dres_prereq_t *
dres_new_prereq(int id)
{
    dres_prereq_t *dep;

    if (ALLOC_OBJ(dep) == NULL)
        return NULL;
    
    if (dres_add_prereq(dep, id) != DRES_ID_NONE)
        return dep;

    free_prereq(dep);
    return NULL;
}


/********************
 * free_prereq
 ********************/
static void
free_prereq(dres_prereq_t *dep)
{
    if (dep) {
        FREE(dep->ids);
        FREE(dep);
    }
}


/*****************************************************************************
 *                          *** variable handling ***                        *
 *****************************************************************************/

/********************
 * dres_add_variable
 ********************/
int
dres_add_variable(dres_t *dres, char *name)
{
    dres_variable_t *var;
    int              id;

    if (!REALLOC_ARR(dres->variables, dres->nvariable, dres->nvariable + 1))
        return DRES_ID_NONE;

    id  = dres->nvariable++;
    var = dres->variables + id;

    var->id   = DRES_VARIABLE(id);
    var->name = STRDUP(name);

    return var->name ? var->id : DRES_ID_NONE;
}


/********************
 * dres_variable_id
 ********************/
int
dres_variable_id(dres_t *dres, char *name)
{
    dres_variable_t *var;
    int              i;

    if (name != NULL)
        for (i = 0, var = dres->variables; i < dres->nvariable; i++, var++) {
            if (!strcmp(name, var->name))
                return var->id;
        }
    
    return dres_add_variable(dres, name);
}


/********************
 * finalize_variables
 ********************/
static int
finalize_variables(dres_t *dres)
{
    dres_variable_t *v;
    int              i;

    for (i = 0, v = dres->variables; i < dres->nvariable; i++, v++)
        if (!(v->var = dres_var_init(dres->fact_store, v->name, &v->stamp)))
            return EIO;
    
    dres_store_finish(dres->fact_store);
    dres_store_finish(dres->dres_store);

    return 0;
}


/********************
 * free_variables
 ********************/
static void
free_variables(dres_t *dres)
{
    int              i;
    dres_variable_t *var;

    for (i = 0, var = dres->variables; i < dres->nvariable; i++, var++) {
        FREE(var->name);
#if 0
        if (var->var)
            dres_var_destroy(var->var);
#endif
    }
    
    FREE(dres->variables);

    dres->variables = NULL;
    dres->nvariable = 0;
}



/*****************************************************************************
 *                        *** target action handling ***                     *
 *****************************************************************************/

/********************
 * dres_new_action
 ********************/
dres_action_t *
dres_new_action(int argument)
{
    dres_action_t *action;
    
    if (ALLOC_OBJ(action) == NULL)
        return NULL;
    
    action->lvalue = DRES_ID_NONE;

    if (argument != DRES_ID_NONE && dres_add_argument(action, argument)) {
        free_action(action);
        return NULL;
    }
    
    return action;
}


/********************
 * dres_add_argument
 ********************/
int
dres_add_argument(dres_action_t *action, int argument)
{
    if (!REALLOC_ARR(action->arguments, action->nargument, action->nargument+1))
        return ENOMEM;

    action->arguments[action->nargument++] = argument;
    
    return 0;
}


/********************
 * dres_add_assignment
 ********************/
int
dres_add_assignment(dres_action_t *action, int var, int val)
{
    if (!REALLOC_ARR(action->variables, action->nvariable, action->nvariable+1))
        return ENOMEM;
    
    action->variables[action->nvariable].var_id = var;
    action->variables[action->nvariable].val_id = val;
    action->nvariable++;
    
    return 0;

}


/********************
 * free_action
 ********************/
static void
free_action(dres_action_t *action)
{
    if (action) {
        FREE(action->name);
        FREE(action->arguments);
        free(action);
    }
}


/********************
 * free_actions
 ********************/
static void
free_actions(dres_action_t *action)
{
    dres_action_t *a, *p;

    if (action == NULL)
        return;

    for (a = action, p = NULL; a->next != NULL; p = a, a = a->next) {
        FREE(p);
        FREE(a->name);
        FREE(a->arguments);
    }

    FREE(p);
}


/********************
 * dres_dump_action
 ********************/
void
dres_dump_action(dres_t *dres, dres_action_t *a)
{
    int  i;
    char buf[32], *t;

    while (a) {
        printf("%s(", a->name);
        for (i = 0, t = ""; i < a->nargument; i++, t=",")
            printf("%s%s", t, dres_name(dres,
                                        a->arguments[i], buf, sizeof(buf)));
        printf(")%s%s\n",
               a->lvalue == DRES_ID_NONE ? "" : " => ",
               a->lvalue == DRES_ID_NONE ? "" : dres_name(dres, a->lvalue,
                                                          buf, sizeof(buf)));
        a = a->next;
    }
}


/*****************************************************************************
 *                          *** literal handling ***                         *
 *****************************************************************************/

/********************
 * dres_add_literal
 ********************/
int
dres_add_literal(dres_t *dres, char *name)
{
    dres_literal_t *l;
    int             id;

    if (!REALLOC_ARR(dres->literals, dres->nliteral, dres->nliteral + 1))
        return DRES_ID_NONE;

    id = dres->nliteral++;
    l  = dres->literals + id;

    l->id   = DRES_LITERAL(id);
    l->name = STRDUP(name);

    return l->name ? l->id : DRES_ID_NONE;
}


/********************
 * dres_literal_id
 ********************/
int
dres_literal_id(dres_t *dres, char *name)
{
    dres_literal_t *l;
    int             i;

    if (name != NULL)
        for (i = 0, l = dres->literals; i < dres->nliteral; i++, l++) {
            if (!strcmp(name, l->name))
                return l->id;
        }
    
    return dres_add_literal(dres, name);
}


/********************
 * free_literls
 ********************/
static void
free_literals(dres_t *dres)
{
    int             i;
    dres_literal_t *l;
    
    for (i = 0, l = dres->literals; i < dres->nliteral; i++, l++)
        FREE(l->name);
        
    FREE(dres->literals);

    dres->literals = NULL;
    dres->nliteral = 0;
}


/*****************************************************************************
 *                        *** dependency graph handling ***                  *
 *****************************************************************************/


/********************
 * dres_build_graph
 ********************/
dres_graph_t *
dres_build_graph(dres_t *dres, char *goal)
{
    dres_graph_t  *graph;
    dres_target_t *target;
    int            prid, i, n;

    graph = NULL;

    if ((target = dres_lookup_target(dres, goal)) == NULL)
        goto fail;

    if (!DRES_IS_DEFINED(target->id))
        goto fail;
    
    if (ALLOC_OBJ(graph) == NULL)
        goto fail;
    
    n = dres->ntarget + dres->nvariable;
    
    if ((graph->depends = ALLOC_ARR(typeof(*graph->depends), n)) == NULL)
        goto fail;

    for (i = 0; i < n; i++)
        graph->depends[i].nid = -1;                /* node not in graph yet */

    graph->ntarget   = dres->ntarget;
    graph->nvariable = dres->nvariable;
    
    if (target->prereqs != NULL) {
        for (i = 0; i < target->prereqs->nid; i++) {
            prid = target->prereqs->ids[i];
            graph_build_prereq(dres, graph, target, prid);
        }
    }
    

    /*
     * make sure targets that have only prerequisites but are not
     * prerequisites themselves (eg. our goal) are also part of the graph
     */
    
    if (graph_add_leafs(dres, graph) != 0)
        goto fail;
   
    return graph;

 fail:
    dres_free_graph(graph);
    return NULL;
}


/********************
 * dres_free_graph
 ********************/
void
dres_free_graph(dres_graph_t *graph)
{
    dres_prereq_t *prq;
    int            i;
    
    if (graph == NULL || graph->depends == NULL)
        return;
    
    for (i = 0; i < graph->ntarget + graph->nvariable; i++) {
        prq = graph->depends + i;
        FREE(prq->ids);
    }
    
    FREE(graph->depends);
    FREE(graph);
}


/********************
 * graph_build_prereq
 ********************/
static int
graph_build_prereq(dres_t *dres,
                   dres_graph_t *graph, dres_target_t *target, int prereq)
{
    dres_target_t *t;
    int            i, status;
    char           name[32];

    if (graph_has_prereq(graph, target->id, prereq))
        return 0;

    DEBUG("0x%x (%s) -> %s", prereq, dres_name(dres, prereq, name,sizeof(name)),
          target->name);
    
    /* add edge prereq -> target */
    if ((status = graph_add_prereq(dres, graph, target->id, prereq)) != 0)
        return status;

    switch (DRES_ID_TYPE(prereq)) {
    case DRES_TYPE_TARGET:
        t = dres->targets + DRES_INDEX(prereq);
        if (t->prereqs != NULL)
            for (i = 0; i < t->prereqs->nid; i++)
                if ((status = graph_build_prereq(dres,
                                                 graph, t,t->prereqs->ids[i])))
                    return status;
        return 0;

    case DRES_TYPE_VARIABLE:
        return 0;
        
    default:
        return EINVAL;
    }
}


/********************
 * graph_add_prereq
 ********************/
static int
graph_add_prereq(dres_t *dres, dres_graph_t *graph, int tid, int prid)
{
    dres_prereq_t *depends;
    int            idx;

    if (DRES_ID_TYPE(prid) == DRES_TYPE_VARIABLE)
        idx = graph->ntarget;
    else
        idx = 0;

    idx    += DRES_INDEX(prid);
    depends = graph->depends + idx;
    
    if (depends->nid < 0)
        depends->nid = 0;                          /* unmark as not present */

    if (!REALLOC_ARR(depends->ids, depends->nid, depends->nid + 1))
        return ENOMEM;

    depends->ids[depends->nid++] = tid;
    return 0;
}


/********************
 * graph_has_prereq
 ********************/
static int
graph_has_prereq(dres_graph_t *graph, int tid, int prid)
{
    dres_prereq_t *prereqs;
    int            idx, i;

    if (DRES_ID_TYPE(prid) == DRES_TYPE_VARIABLE)
        idx = graph->ntarget;
    else
        idx = 0;

    idx    += DRES_INDEX(prid);
    prereqs = graph->depends + idx;
    
    for (i = 0; i < prereqs->nid; i++)
        if (prereqs->ids[i] == tid)
            return TRUE;
    
    return FALSE;
}


/********************
 * graph_add_leafs
 ********************/    
static int
graph_add_leafs(dres_t *dres, dres_graph_t *graph)
{
    dres_prereq_t *prq, *target;
    int            i, j, id;
    char           buf[32];
            

    /* */
    for (i = 0; i < graph->ntarget; i++) {
        prq = graph->depends + i;
        for (j = 0; j < prq->nid; j++) {
            id = prq->ids[j];
            
            if (DRES_ID_TYPE(id) == DRES_TYPE_TARGET) {
                target = graph->depends + DRES_INDEX(id);
                if (target->nid < 0) {
                    target->nid = 0;            /* unmark as not present */
                    DEBUG("leaf target %s (0x%x) pulled in",
                          dres_name(dres, id, buf, sizeof(buf)), id);
                }
            }
        }
    }


    for (i = graph->ntarget; i < graph->nvariable; i++) {
        prq = graph->depends + graph->ntarget + i;
        for (j = 0; j < prq->nid; j++) {
            id = prq->ids[j];
            if (DRES_ID_TYPE(id) == DRES_TYPE_TARGET) {
                target = graph->depends + DRES_INDEX(id);
                if (target->nid < 0) {
                    target->nid = 0;              /* unmark as not present */
                    DEBUG("leaf target %s (0x%x) pulled in",
                          dres_name(dres, id, buf, sizeof(buf)), id);
                }
            }
        }
    }
    
    return 0;
}


/********************
 * dres_sort_graph
 ********************/
int *
dres_sort_graph(dres_t *dres, dres_graph_t *graph)
{
    
    /*
     * Notes:
     *
     *    I sincerely apologize to mankind the existence of this code...
     *    It is not so much the code but more the whole hairy combination
     *    of the code, the data structures and their (mis)use. I promise
     *    to really clean it up once we have the basic infrastucture pulled
     *    together and somewhat working functionally.
     *
     *    You have been warned...
     *
     *    Lasciate ogne speranza, voi ch'intrate...
     *
     *    Abandon all hope, ye who enter here...
     *
     *    Not to mention ye who dares to touch this...
     */
    


    /*
     * Notes #2:
     *
     *   We sort our dependency graph toplogically to determine one of
     *   the possible check orders. We attempt to follow the principles
     *   of the following algorithm:
     *
     *       L <- empty list where we put the sorted elements
     *       Q <- set of all nodes with no incoming edges
     *       while Q is non-empty do
     *           remove a node n from Q
     *           insert n into L
     *           for each node m with an edge e from n to m do
     *               remove edge e from the graph
     *               if m has no other incoming edges then
     *                   insert m into Q
     *       if graph has edges then
     *           output error message (graph has a cycle)
     *       else 
     *           output message (proposed topologically sorted order: L)
     */




#define PUSH(q, item) do {                                    \
        char buf[32];                                         \
                                                              \
        int __t = t##q;                                       \
        int __size = n;                                       \
                                                              \
        DEBUG("PUSH(%s, %s), as item #%d...", #q,             \
              dres_name(dres, item, buf, sizeof(buf)),  __t); \
        q[__t++]  =   item;                                   \
        __t      %= __size;                                   \
        t##q = __t;                                           \
    } while (0)
    
            

#define POP(q) ({                                                  \
            int __h = h##q, __t = t##q;                            \
            int __size = n;                                        \
            int __item = DRES_ID_NONE;                             \
                                                                   \
            if (__h != __t) {                                      \
                __item = q[__h++];                                 \
                __h %= __size;                                     \
            }                                                      \
            DEBUG("POP(%s): %s, head is #%d...", #q,               \
                  dres_name(dres, __item, buf, sizeof(buf)), __h); \
            h##q = __h;                                            \
            __item;                                                \
        })

#define NEDGE(id) \
    (E + DRES_INDEX(id) + \
     (DRES_ID_TYPE(id) == DRES_TYPE_VARIABLE ? graph->ntarget : 0))

#define PRQ_IDX(id) \
    (DRES_INDEX(id) +                                           \
     (DRES_ID_TYPE(id) == DRES_TYPE_VARIABLE ? graph->ntarget : 0))

    int *L, *Q, *E;
    int  hL, hQ, tL, tQ;
    int  node, status;

    dres_prereq_t *prq;
    int            i, j, n;
    char           buf[32], buf1[32];
    

    L = Q = NULL;
    n = graph->ntarget + graph->nvariable;
    
    if ((L = malloc((n+1) * sizeof(*L))) == NULL ||
        (Q = malloc( n    * sizeof(*Q))) == NULL ||
        (E = malloc( n    * sizeof(*E))) == NULL) {
        status = ENOMEM;
        goto fail;
    }
    memset(L, DRES_ID_NONE, (n+1) * sizeof(*L));
    memset(Q, DRES_ID_NONE,  n    * sizeof(*Q));
    memset(E, 0           ,  n    * sizeof(*E));
    
    hL = tL = hQ = tQ = 0;

    /* initialize L, incoming edges / node */
    status = 0;
    for (i = 0; i < graph->nvariable; i++) {
        prq = graph->depends + graph->ntarget + i;
        if (prq->nid == -1)                     /* not in the graph at all */
            continue;
        
        if (dres->variables[i].id != DRES_VARIABLE(i)) {
            DEBUG("##### %s@%s:%d: BUG, unexpected variable ID:",
                  __FUNCTION__, __FILE__, __LINE__);
            DEBUG("##### 0x%x != 0x%x", dres->variables[i].id,DRES_VARIABLE(i));
            status = EINVAL;
        }
        
        PUSH(Q, dres->variables[i].id); /* variables don't depend on anything */
        
        for (j = 0; j < prq->nid; j++) {
            DEBUG("edge %s -> %s",
                  dres_name(dres, DRES_VARIABLE(i), buf, sizeof(buf)),
                  dres_name(dres, prq->ids[j], buf1, sizeof(buf1)));
            
            if (*NEDGE(prq->ids[j]) < 0)
                *NEDGE(prq->ids[j]) = 1;
            else
                *NEDGE(prq->ids[j]) += 1;
        }
        
    }
    
    for (i = 0; i < graph->ntarget; i++) {
        dres_target_t *t;

        prq = graph->depends + i;
        t   = dres->targets + i;
        
        if (prq->nid == -1)                     /* not in the graph at all */
            continue;
        
        DEBUG("checking target #%d (%s)...", i,
              dres_name(dres, DRES_TARGET(i), buf, sizeof(buf)));

        if (dres->targets[i].id != DRES_TARGET(i)) {
            DEBUG("### %s@%s:%d: BUG, unexpected target ID:",
                  __FUNCTION__, __FILE__, __LINE__);
            DEBUG("### 0x%x != 0x%x", dres->targets[i].id, DRES_TARGET(i));
            status = EINVAL;
        }

        if (t-> prereqs != NULL && t->prereqs->nid == 0)
            PUSH(Q, t->id);
        
        for (j = 0; j < prq->nid; j++) {
            DEBUG("edge %s -> %s",
                  dres_name(dres, DRES_TARGET(i), buf, sizeof(buf)),
                  dres_name(dres, prq->ids[j], buf1, sizeof(buf1)));
                   
            if (*NEDGE(prq->ids[j]) < 0)
                *NEDGE(prq->ids[j]) = 1;
            else
                *NEDGE(prq->ids[j]) += 1;
        }
    }

    
    for (i = 0; i < dres->ntarget; i++)
        DEBUG("E[%s] = %d",
              dres_name(dres, dres->targets[i].id, buf, sizeof(buf)),
              *NEDGE(dres->targets[i].id));
    
    for (i = 0; i < dres->nvariable; i++)
        DEBUG("E[%s] = %d",
              dres_name(dres, dres->variables[i].id, buf, sizeof(buf)),
              *NEDGE(dres->variables[i].id));
    
    
    /* try to sort topologically the graph */
    hQ = hL = 0;
    while ((node = POP(Q)) != DRES_ID_NONE) {
        PUSH(L, node);
        prq = graph->depends + PRQ_IDX(node);
        for (i = 0; i < prq->nid; i++) {
            if (!DRES_IS_DELETED(prq->ids[i])) {
                DEBUG("  DELETE edge %s -> %s",
                      dres_name(dres, node, buf, sizeof(buf)),
                      dres_name(dres, prq->ids[i], buf1, sizeof(buf1)));
                prq->ids[i] = DRES_DELETED(prq->ids[i]);
                if (*NEDGE(prq->ids[i]) == 1) {
                    *NEDGE(prq->ids[i]) = 0;
                    PUSH(Q, prq->ids[i]);
                }
                else
                    *NEDGE(prq->ids[i]) -= 1;
                
                DEBUG("  # of edges to %s: %d",
                      dres_name(dres, prq->ids[i], buf, sizeof(buf)),
                      *NEDGE(prq->ids[i]));
            }
            else {
                DEBUG("  edge %s -> %s already deleted",
                      dres_name(dres, node, buf, sizeof(buf)),
                      dres_name(dres, prq->ids[i], buf1, sizeof(buf1)));
            }
        }
    }


    /* check that we exhausted all edges */
    for (i = 0; i < n; i++) {
        if (E[i] != 0) {
            DEBUG("error: graph has cycles");
            DEBUG("still has %d edges for %s #%d", E[i],
                   i < graph->ntarget ? "target" : "variable",
                   i < graph->ntarget ? i : i - graph->ntarget);
            status = EINVAL;
        }
    }
    

    if (status == 0)
        return L;
    
    
 fail:
    if (L)
        free(L);
    if (Q)
        free(Q);
    if (E)
        free(E);
    
    return NULL;
}



/********************
 * dres_update_goal
 ********************/
int
dres_update_goal(dres_t *dres, char *goal)
{
    dres_graph_t  *graph;
    dres_target_t *target;
    int           *list, id, i;

    graph = NULL;
    list  = NULL;

#if 1
    dres_store_update_timestamps(dres->fact_store, ++(dres->stamp));
#endif

    if ((target = dres_lookup_target(dres, goal)) == NULL)
        goto fail;
    
    if (!DRES_IS_DEFINED(target->id))
        goto fail;
    
    if (target->prereqs == NULL) {
        DEBUG("%s has no prerequisites => needs to be updated", target->name);
        execute_actions(dres, target);
        target->stamp = dres->stamp;
        return 0;
    }

    if ((graph = dres_build_graph(dres, goal)) == NULL)
        goto fail;
    
    if ((list = dres_sort_graph(dres, graph)) == NULL)
        goto fail;

    printf("topological sort for goal %s:\n", goal);
    dres_dump_sort(dres, list);
    
    for (i = 0; list[i] != DRES_ID_NONE; i++) {
        id = list[i];

        if (DRES_ID_TYPE(id) != DRES_TYPE_TARGET)
            continue;
        
        check_target(dres, id);
    }

    free(list);
    dres_free_graph(graph);

    return 0;

 fail:
    if (list)
        free(list);
    dres_free_graph(graph);
    
    return EINVAL;
}


/********************
 * check_variable
 ********************/
static int
check_variable(dres_t *dres, int id, int refstamp)
{
    dres_variable_t *var = dres->variables + DRES_INDEX(id);
    char             buf[32];
    
    DEBUG("%s: %d > %d ?", dres_name(dres, id, buf, sizeof(buf)),
          var->stamp, refstamp);
    
#ifdef STAMP_FORCED_UPDATE
    var->stamp = dres->stamp+1;          /* fake that variables have changed */
    return TRUE;
#endif
    
    return var->stamp > refstamp;
}


/********************
 * check_target
 ********************/
static int
check_target(dres_t *dres, int tid)
{
    dres_target_t *target, *t;
    dres_prereq_t *prq;
    int            i, id, update;
    char           buf[32];

    DEBUG("checking target %s", dres_name(dres, tid, buf, sizeof(buf)));

    target = dres->targets + DRES_INDEX(tid);
    
    if ((prq = target->prereqs) == NULL)
        update = TRUE;
    else {
        update = FALSE;
        for (i = 0; i < prq->nid; i++) {
            id = prq->ids[i];
            switch (DRES_ID_TYPE(id)) {
            case DRES_TYPE_VARIABLE:
                if (check_variable(dres, id, target->stamp)) {
                    DEBUG("=> newer, %s needs to be updated", target->name);
                    update = TRUE;
                }
                break;
            case DRES_TYPE_TARGET:
                t = dres->targets + DRES_INDEX(id);
                DEBUG("%s: %d > %s: %d ?",
                      target->name, target->stamp, t->name, t->stamp);
                if (t->stamp > target->stamp) {
                    DEBUG("=> %s newer, %s needs to be updates", t->name,
                          target->name);
                    update = TRUE;
                }
                break;
            default:
                DEBUG("### BUG: invalid prereq 0x%x for %s", id, target->name);
                break;
            }
        }
    }
        
    if (update) {
        execute_actions(dres, target);
        target->stamp = dres->stamp;
    }
    
    return update;
}


/********************
 * execute_actions
 ********************/
static int
execute_actions(dres_t *dres, dres_target_t *target)
{
    dres_action_t *a;
    dres_assign_t *v;
    int            i, j;
    char           buf[32], *t;

    if (target->actions == NULL)
        return 0;

    DEBUG("executing actions for %s", target->name);

    for (a = target->actions; a; a = a->next) {
        printf("[%s]    %s%s%s(", __FUNCTION__,
               a->lvalue != DRES_ID_NONE ?
               dres_name(dres, a->lvalue, buf, sizeof(buf)): "",
               a->lvalue != DRES_ID_NONE ? " = " : "", a->name);
        for (i = 0, t = ""; i < a->nargument; i++, t=",")
            printf("%s%s", t,
                   dres_name(dres, a->arguments[i], buf, sizeof(buf)));
        for (j = 0, v = a->variables; j < a->nvariable; j++, v++, t=",") {
            char var[32], val[32];
            printf("%s%s=%s", t,
                   dres_name(dres, v->var_id, var, sizeof(var)),
                   dres_name(dres, v->val_id, val, sizeof(val)));
        }

        printf(")\n");

        if (!strcmp(a->name, "dres")) {
            char *goal = dres_name(dres, a->arguments[0], buf, sizeof(buf));
            DEBUG("##### recursing for goal %s...", goal);
            dres_update_goal(dres, goal);
            DEBUG("##### back from recursive dres(%s)", goal);
        }
    }
    
    return 0;
}





/*****************************************************************************
 *                       *** misc. dumping/debugging routines                *
 *****************************************************************************/


/********************
 * dres_name
 ********************/
char *
dres_name(dres_t *dres, int id, char *buf, size_t bufsize)
{
    dres_target_t   *target;
    dres_variable_t *variable;
    dres_literal_t  *literal;

    switch (DRES_ID_TYPE(id)) {
        
    case DRES_TYPE_TARGET:
        target = dres->targets + DRES_INDEX(id);
        snprintf(buf, bufsize, "%s", target->name);
        break;
        
    case DRES_TYPE_VARIABLE:
        variable = dres->variables + DRES_INDEX(id);
        snprintf(buf, bufsize, "$%s", variable->name);
        break;

    case DRES_TYPE_LITERAL:
        literal = dres->literals + DRES_INDEX(id);
        snprintf(buf, bufsize, "%s", literal->name);
        break;

    default:
        snprintf(buf, bufsize, "<invalid id 0x%x>", id);
    }

    return buf;
}



/********************
 * dres_dump_targets
 ********************/
void
dres_dump_targets(dres_t *dres)
{
    int            i, j, id, idx;
    dres_target_t *t;
    dres_prereq_t *d;
    dres_action_t *a;
    dres_assign_t *v;
    char          *_t;
    int            n;
    
    printf("Found %d targets:\n", dres->ntarget);

    for (i = 0, t = dres->targets; i < dres->ntarget; i++, t++) {
        printf("target #%d: %s (0x%x, %p, %p)\n", i, t->name, t->id,
               t->prereqs, t->actions);
        if ((d = t->prereqs) != NULL) {
            for (j = 0; j < d->nid; j++) {
                id  = d->ids[j];
                idx = DRES_INDEX(id);
                switch (DRES_ID_TYPE(id)) {
                case DRES_TYPE_TARGET:
                    printf("  depends on target %s\n",
                           dres->targets[idx].name);
                    break;
                case DRES_TYPE_VARIABLE:
                    printf("  depends on variable $%s\n",
                           dres->variables[idx].name);
                    break;
                default:
                    printf("  depends on unknown object 0x%x\n", id);
                }
            }
        }

        if (t->actions == NULL) {
            printf("  has no actions\n");
            continue;
        }
        
        for (a = t->actions; a; a = a->next) {
            char buf[32];

            printf("  has action %s%s%s(",
                   a->lvalue != DRES_ID_NONE ?
                   dres_name(dres, a->lvalue, buf, sizeof(buf)): "",
                   a->lvalue != DRES_ID_NONE ? " = " : "", a->name);
            for (n = 0, _t=""; n < a->nargument; n++, _t=",") {
                id  = a->arguments[n];
                idx = DRES_INDEX(id);
                switch (DRES_ID_TYPE(id)) {
                case DRES_TYPE_VARIABLE:
                    printf("%s$%s", _t, dres->variables[idx].name);
                    break;
                case DRES_TYPE_LITERAL:
                    printf("%s%s", _t, dres->literals[idx].name);
                    break;
                default:
                    printf("%s<unknown>", _t);
                }
            }

            for (j = 0, v = a->variables; j < a->nvariable; j++, v++, _t=",") {
                char var[32], val[32];
                printf("%s%s=%s", _t,
                       dres_name(dres, v->var_id, var, sizeof(var)),
                       dres_name(dres, v->val_id, val, sizeof(val)));
            }
            
            printf(")\n");
        }
    }
}


/********************
 * dres_dump_sort
 ********************/
void
dres_dump_sort(dres_t *dres, int *list)
{
    int  i;
    char buf[32];
   
    for (i = 0; list[i] != DRES_ID_NONE; i++)
        printf("  #%03d: 0x%x (%s)\n", i, list[i],
               dres_name(dres, list[i], buf, sizeof(buf)));
}





/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
