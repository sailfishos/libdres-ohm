#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <prolog/ohm-fact.h>

#include <dres/dres.h>
#include "parser.h"

#undef STAMP_FORCED_UPDATE

extern FILE *yyin;
extern int   yyparse(dres_t *dres);

static void free_literals(dres_t *dres);
static int  finalize_variables(dres_t *dres);
static int  finalize_actions(dres_t *dres);

static void free_prereq(dres_prereq_t *dep);

static int graph_build_prereq(dres_t *dres, dres_graph_t *graph,
                              dres_target_t *target, int prereq);
static int graph_has_prereq(dres_graph_t *graph, int tid, int prid);
static int graph_add_prereq(dres_t *dres, dres_graph_t *graph,int tid,int prid);
static int graph_add_leafs(dres_t *dres, dres_graph_t *graph);



static int execute_actions(dres_t *dres, dres_target_t *target);

int depth = 0;


/********************
 * dres_init
 ********************/
dres_t *
dres_init(void)
{
    dres_t *dres;
    int     status;

    if (ALLOC_OBJ(dres) == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    
    dres->fact_store = dres_store_init(STORE_FACT , "com.nokia.policy");
    dres->dres_store = dres_store_init(STORE_LOCAL, NULL);

    if ((status = dres_register_builtins(dres)) != 0)
        goto fail;
    
    if (dres->fact_store == NULL || dres->dres_store == NULL)
        goto fail;

    dres->stamp = 1;

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
    
    dres_free_targets(dres);
    dres_free_variables(dres);
    free_literals(dres);
    
    dres_store_destroy(dres->fact_store);
    dres_store_destroy(dres->dres_store);

    FREE(dres);
}


/********************
 * dres_parse_file
 ********************/
int
dres_parse_file(dres_t *dres, char *path)
{
    int status;
    
    if (path == NULL)
        return EINVAL;
    
    if ((yyin = fopen(path, "r")) == NULL)
        return errno;
    
    status = yyparse(dres);
    fclose(yyin);

    if (status == 0)
        status = finalize_variables(dres);
    
    return status;
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
 * finalize_actions
 ********************/
static int
finalize_actions(dres_t *dres)
{
    dres_target_t  *target;
    dres_action_t  *action;
    int             i;
    
    for (i = 0, target = dres->targets; i < dres->ntarget; i++, target++)
        for (action = target->actions; action; action = action->next)
            if (!(action->handler = dres_lookup_handler(dres, action->name)))
                return ENOENT;

    DRES_SET_FLAG(dres, ACTIONS_FINALIZED);
    return 0;
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
 * free_literals
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
    

    L = Q = E = NULL;
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

#if 0 /* hmm... t->prereqs == NULL also indicates no incoming edges */
        if (t->prereqs != NULL && t->prereqs->nid == 0)
            PUSH(Q, t->id);
#else
        if (t->prereqs == NULL || t->prereqs->nid == 0)
            PUSH(Q, t->id);
#endif
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
    int           *list, id, i, status;

    graph = NULL;
    list  = NULL;

    if (!DRES_TST_FLAG(dres, ACTIONS_FINALIZED))
        if ((status = finalize_actions(dres)) != 0)
            return EINVAL;
    
#if 1
    dres_store_update_timestamps(dres->fact_store, ++(dres->stamp));
#endif

    if ((target = dres_lookup_target(dres, goal)) == NULL)
        goto fail;
    
    if (!DRES_IS_DEFINED(target->id))
        goto fail;
    
    if (target->prereqs == NULL) {
        DEBUG("%s has no prerequisites => needs to be updated", target->name);
        dres_run_actions(dres, target);
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
        
        dres_check_target(dres, id);
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


/********************
 * yyerror
 ********************/
void
yyerror(dres_t *dres, const char *msg)
{
    dres_parse_error(dres, msg, yylval.string);
}





/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
