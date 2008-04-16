#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "dres.h"

#define STAMP_FORCED_UPDATE

#ifndef TRUE
#    define FALSE 0
#    define TRUE  1
#endif

#define DEBUG(fmt, args...) do {                                \
        printf("[%s] "fmt"\n", __FUNCTION__, ## args); \
    } while (0)

extern FILE *yyin;
extern int   yyparse(void);

static dres_target_t   *targets   = NULL;
static int              ntarget   = 0;
static dres_variable_t *variables = NULL;
static int              nvariable = 0;
static dres_literal_t  *literals  = NULL;
static int              nliteral  = 0;

static int              stamp     = 1;

static void free_targets(void);
static void free_variables(void);
static void free_literals(void);

static void free_action (dres_action_t *action);
static void free_actions(dres_action_t *actions);

static void free_prereq(dres_prereq_t *dep);

static int graph_build_prereq(dres_graph_t *graph, dres_target_t *target,
                              int prereq);
static int graph_has_prereq(dres_graph_t *graph, int tid, int prid);
static int graph_add_prereq(dres_graph_t *graph, int tid, int prid);
static int graph_add_leafs(dres_graph_t *graph);

static int check_variable(int id, int stamp);
static int check_target(int id);

static int execute_actions(dres_target_t *target);




/********************
 * dres_init
 ********************/
int
dres_init(char *path)
{
    int status;
    
    if (path == NULL)
        yyin = stdin;
    else
        if ((yyin = fopen(path, "r")) == NULL)
            return errno;
    
    status = yyparse();
    
    if (yyin != stdin)
        fclose(yyin);

    return status;
}


/********************
 * dres_exit
 ********************/
void
dres_exit(void)
{
    free_targets();
    free_variables();
    free_literals();
}


/**************************************************
 * target handling
 **************************************************/

/********************
 * dres_add_target
 ********************/
int
dres_add_target(char *name)
{
    dres_target_t *t = NULL;
    int            id;

    if (targets != NULL) {
        dres_target_t *p;
        if ((p = realloc(targets, sizeof(*targets)*(ntarget+1))) == NULL)
            return DRES_ID_NONE;
        targets = p;
    }
    else {
        if ((targets = malloc(sizeof(*targets))) == NULL)
            return DRES_ID_NONE;
    }

    id = ntarget++;
    t  = targets + id;
    memset(t, 0, sizeof(*t));
    t->id = DRES_UNDEFINED(DRES_TARGET(id));
    
    if ((t->name = strdup(name)) == NULL)
        return DRES_ID_NONE;
    
    return t->id;
}


/********************
 * dres_target_id
 ********************/
int
dres_target_id(char *name)
{
    dres_target_t *t;
    int            i, id;

    id = DRES_ID_NONE;
    if (name != NULL)
        for (i = 0, t = targets; i < ntarget; i++, t++)
            if (!strcmp(name, t->name)) {
                id = t->id;
                break;
            }

    if (id == DRES_ID_NONE)
        id = dres_add_target(name);
    
    return id;
}


/********************
 * dres_lookup_target
 ********************/
dres_target_t *
dres_lookup_target(char *name)
{
    dres_target_t *t;
    int            i;
    
    for (i = 0, t = targets; i < ntarget; i++, t++) {
        if (!strcmp(t->name, name))
            return t;
    }

    i = dres_target_id(name);
    t = targets + DRES_INDEX(i);
    return t;
}


/********************
 * free_targets
 ********************/
void
free_targets(void)
{
    int            i;
    dres_target_t *t;

    for (i = 0, t = targets; i < ntarget; i++, t++) {
        if (t->name)
            free(t->name);
        if (t->prereqs)
            free(t->prereqs);
        free_actions(t->actions);
    }

    targets = NULL;
    ntarget = 0;
}


/**************************************************
 * prerequisite handling
 **************************************************/

/********************
 * dres_add_prereq
 ********************/
int
dres_add_prereq(dres_prereq_t *dep, int id)
{
    if (dep->nid < 0)                              /* unmark as not present */
        dep->nid = 0;

    if (dep->ids != NULL) {
        int *p;
        if ((p = realloc(dep->ids, sizeof(*(dep->ids))*(dep->nid + 1))) == NULL)
            return DRES_ID_NONE;
        dep->ids = p;
    }
    else {
        if ((dep->ids = malloc(sizeof(*(dep->ids)))) == NULL)
            return DRES_ID_NONE;
    }

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

    if ((dep = malloc(sizeof(*dep))) == NULL)
        return NULL;
    
    memset(dep, 0, sizeof(*dep));
    
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
        if (dep->ids)
            free(dep->ids);
        free(dep);
    }
}



/**************************************************
 * variable handling
 **************************************************/

/********************
 * dres_add_variable
 ********************/
int
dres_add_variable(char *name)
{
    dres_variable_t *v = NULL;
    int              id;

    if (variables != NULL) {
        dres_variable_t *p;
        if ((p = realloc(variables, sizeof(*variables)*(nvariable+1))) == NULL)
            return DRES_ID_NONE;
        variables = p;
    }
    else {
        if ((variables = malloc(sizeof(*variables))) == NULL)
            return DRES_ID_NONE;
    }

    id = nvariable++;
    v  = variables + id;
    memset(v, 0, sizeof(*v));
    v->id = DRES_VARIABLE(id);
    
    if ((v->name = strdup(name)) == NULL)
        return DRES_ID_NONE;
    
    return v->id;
}


/********************
 * dres_variable_id
 ********************/
int
dres_variable_id(char *name)
{
    dres_variable_t *v;
    int              i, id;

    id = DRES_ID_NONE;
    if (name != NULL)
        for (i = 0, v = variables; i < nvariable; i++, v++)
            if (!strcmp(name, v->name)) {
                id = v->id;
                break;
            }

    if (id == DRES_ID_NONE)
        id = dres_add_variable(name);
    
    return id;
}


/********************
 * free_variables
 ********************/
static void
free_variables(void)
{
    int              i;
    dres_variable_t *v;

    if (variables) {
        for (i = 0, v = variables; i < nvariable; i++, v++) {
            if (v->name)
                free(v->name);
        }
        free(variables);
    }

    variables = NULL;
    nvariable = 0;
}


/**************************************************
 * action handling
 **************************************************/

/********************
 * dres_new_action
 ********************/
dres_action_t *
dres_new_action(int argument)
{
    dres_action_t *action;
    
    if ((action = malloc(sizeof(*action))) == NULL)
        return NULL;
    
    memset(action, 0, sizeof(*action));
    
    if (argument != DRES_ID_NONE)
        if (dres_add_argument(action, argument)) {
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
    
    if (action->arguments == NULL) {
        if ((action->arguments = malloc(sizeof(*(action->arguments)))) == NULL)
            return ENOMEM;
    }
    else {
        int *p = realloc(action->arguments,
                         sizeof(*(action->arguments)) * (action->nargument+1));
        if (p == NULL)
            return ENOMEM;
        
        action->arguments = p;
    }
    
    action->arguments[action->nargument++] = argument;
    
    return 0;
}


/********************
 * free_action
 ********************/
static void
free_action(dres_action_t *action)
{
    if (action) {
        if (action->name)
            free(action->name);
        if (action->arguments)
            free(action->arguments);
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
        if (p)
            free(p);
        if (a->name)
            free(a->name);
        if (a->arguments)
            free(a->arguments);
    }

    if (p) {
        if (p->name)
            free(p->name);
        if (p->arguments)
            free(p->arguments);
        free(p);
    }
}


/********************
 * dres_dump_action
 ********************/
void
dres_dump_action(dres_action_t *a)
{
    int  i;
    char buf[32], *t;

    while (a) {
        printf("%s(", a->name);
        for (i = 0, t = ""; i < a->nargument; i++, t=",")
            printf("%s%s", t, dres_name(a->arguments[i], buf, sizeof(buf)));
        printf(")\n");

        a = a->next;
    }
}

/**************************************************
 * literal handling
 **************************************************/

/********************
 * dres_add_literal
 ********************/
int
dres_add_literal(char *name)
{
    dres_literal_t *l = NULL;
    int             id;

    if (literals != NULL) {
        dres_literal_t *p;
        if ((p = realloc(literals, sizeof(*literals)*(nliteral + 1))) == NULL)
            return DRES_ID_NONE;
        literals = p;
    }
    else {
        if ((literals = malloc(sizeof(*literals))) == NULL)
            return DRES_ID_NONE;
    }

    id = nliteral++;
    l  = literals + id;
    memset(l, 0, sizeof(*l));
    l->id = DRES_LITERAL(id);
    
    if ((l->name = strdup(name)) == NULL)
        return DRES_ID_NONE;
    
    return l->id;
}


/********************
 * dres_literal_id
 ********************/
int
dres_literal_id(char *name)
{
    dres_literal_t *l;
    int             i, id;

    id = DRES_ID_NONE;
    if (name != NULL)
        for (i = 0, l = literals; i < nliteral; i++, l++)
            if (!strcmp(name, l->name)) {
                id = l->id;
                break;
            }
    
    if (id == DRES_ID_NONE)
        id = dres_add_literal(name);

    return id;
}


/********************
 * free_literls
 ********************/
static void
free_literals(void)
{
    int             i;
    dres_literal_t *l;
    
    if (literals) {
        for (i = 0, l = literals; i < nliteral; i++, l++) {
            if (l->name)
                free(l->name);
        }
        
        free(literals);
    }

    literals = NULL;
    nliteral = 0;
}



/**************************************************
 * graph and miscallaneous processing 
 **************************************************/

/********************
 * dres_name
 ********************/
char *
dres_name(int id, char *buf, size_t bufsize)
{
    dres_target_t   *target;
    dres_variable_t *variable;
    dres_literal_t  *literal;

    switch (DRES_ID_TYPE(id)) {
        
    case DRES_TYPE_TARGET:
        target = targets + DRES_INDEX(id);
        snprintf(buf, bufsize, "%s", target->name);
        break;
        
    case DRES_TYPE_VARIABLE:
        variable = variables + DRES_INDEX(id);
        snprintf(buf, bufsize, "$%s", variable->name);
        break;

    case DRES_TYPE_LITERAL:
        literal = literals + DRES_INDEX(id);
        snprintf(buf, bufsize, "'%s'", literal->name);
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
dres_dump_targets(void)
{
    int            i, j, id, idx;
    dres_target_t *t;
    dres_prereq_t *d;
    dres_action_t *a;
    char          *_t;
    int            n;
    
    printf("Found %d targets:\n", ntarget);

    for (i = 0, t = targets; i < ntarget; i++, t++) {
        printf("target #%d: %s (0x%x, %p, %p)\n", i, t->name, t->id,
               t->prereqs, t->actions);
        if ((d = t->prereqs) != NULL) {
            for (j = 0; j < d->nid; j++) {
                id  = d->ids[j];
                idx = DRES_INDEX(id);
                switch (DRES_ID_TYPE(id)) {
                case DRES_TYPE_TARGET:
                    printf("  depends on target %s\n", targets[idx].name);
                    break;
                case DRES_TYPE_VARIABLE:
                    printf("  depends on variable $%s\n", variables[idx].name);
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
            printf("  has action %s(", a->name);
            for (n = 0, _t=""; n < a->nargument; n++, _t=",") {
                id  = a->arguments[n];
                idx = DRES_INDEX(id);
                switch (DRES_ID_TYPE(id)) {
                case DRES_TYPE_VARIABLE:
                    printf("%s$%s", _t, variables[idx].name);
                    break;
                case DRES_TYPE_LITERAL:
                    printf("%s%s", _t, literals[idx].name);
                    break;
                default:
                    printf("%s<unknown>", _t);
                }
            }
            printf(")\n");
        }
    }    
}


/********************
 * dres_build_graph
 ********************/
dres_graph_t *
dres_build_graph(char *goal)
{
    dres_graph_t  *graph;
    dres_target_t *target;
    int            prid, i, n;

    graph = NULL;

    if ((target = dres_lookup_target(goal)) == NULL)
        goto fail;

    if (!DRES_IS_DEFINED(target->id))
        goto fail;
    
    if ((graph = malloc(sizeof(*graph))) == NULL)
        goto fail;
    memset(graph, 0, sizeof(*graph));

    n = ntarget + nvariable;
    if ((graph->depends = malloc(n * sizeof(*graph->depends))) == NULL)
        goto fail;
    memset(graph->depends, 0, n * sizeof(*graph->depends));
    for (i = 0; i < n; i++)
        graph->depends[i].nid = -1;                /* node not in graph yet */

    graph->ntarget   = ntarget;
    graph->nvariable = nvariable;
    
    for (i = 0; i < target->prereqs->nid; i++) {
        prid = target->prereqs->ids[i];
        graph_build_prereq(graph, target, prid);
    }
    

    /*
     * make sure targets that have only prerequisites but are not
     * prerequisites themselves (eg. our goal) are also part of the graph
     */
    
    if (graph_add_leafs(graph) != 0)
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
    
    for (i = 0; i < ntarget + nvariable; i++) {
        prq = graph->depends + i;
        if (prq->ids)
            free(prq->ids);
    }
    
    free(graph->depends);
    free(graph);
}


/********************
 * graph_build_prereq
 ********************/
static int
graph_build_prereq(dres_graph_t *graph, dres_target_t *target, int prereq)
{
    dres_target_t *t;
    int            i, status;
    char           name[32];

    if (graph_has_prereq(graph, target->id, prereq))
        return 0;

    DEBUG("0x%x (%s) -> %s", prereq, dres_name(prereq, name, sizeof(name)),
          target->name);
    
    /* add edge prereq -> target */
    if ((status = graph_add_prereq(graph, target->id, prereq)) != 0)
        return status;

    switch (DRES_ID_TYPE(prereq)) {
    case DRES_TYPE_TARGET:
        t = targets + DRES_INDEX(prereq);
        for (i = 0; i < t->prereqs->nid; i++)
            if ((status = graph_build_prereq(graph, t, t->prereqs->ids[i])))
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
graph_add_prereq(dres_graph_t *graph, int tid, int prid)
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

    if (depends->nid == 0) {
        if ((depends->ids = malloc(sizeof(*depends->ids))) == NULL)
            return ENOMEM;
    }
    else {
        int *p = depends->ids;
        if ((p = realloc(p, (depends->nid + 1) * sizeof(*p))) == NULL)
            return ENOMEM;
        depends->ids = p;
    }

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
graph_add_leafs(dres_graph_t *graph)
{
    dres_prereq_t *prq, *target;
    int            i, j, id;
    char           buf[32];
            

    /* */
    for (i = 0; i < ntarget; i++) {
        prq = graph->depends + i;
        for (j = 0; j < prq->nid; j++) {
            id = prq->ids[j];
            
            if (DRES_ID_TYPE(id) == DRES_TYPE_TARGET) {
                target = graph->depends + DRES_INDEX(id);
                if (target->nid < 0) {
                    target->nid = 0;            /* unmark as not present */
                    DEBUG("leaf target %s (0x%x) pulled in",
                          dres_name(id, buf, sizeof(buf)), id);
                }
            }
        }
    }


    for (i = ntarget; i < nvariable; i++) {
        prq = graph->depends + ntarget + i;
        for (j = 0; j < prq->nid; j++) {
            id = prq->ids[j];
            if (DRES_ID_TYPE(id) == DRES_TYPE_TARGET) {
                target = graph->depends + DRES_INDEX(id);
                if (target->nid < 0) {
                    target->nid = 0;              /* unmark as not present */
                    DEBUG("leaf target %s (0x%x) pulled in",
                          dres_name(id, buf, sizeof(buf)), id);
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
dres_sort_graph(dres_graph_t *graph)
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




#define PUSH(q, item) do {                   \
        char buf[32];                        \
                                             \
        int __t = t##q;                      \
        int __size = n;                      \
                                             \
        DEBUG("PUSH(%s, %s), as item #%d...", #q,                       \
              dres_name(item, buf, sizeof(buf)),  __t);                 \
        q[__t++]  =   item;                  \
        __t      %= __size;                  \
        t##q = __t;                          \
    } while (0)
    
            

#define POP(q) ({                              \
            int __h = h##q, __t = t##q;        \
            int __size = n;                    \
            int __item = DRES_ID_NONE;         \
                                               \
            if (__h != __t) {                  \
                __item = q[__h++];             \
                __h %= __size;                 \
            }                                  \
            DEBUG("POP(%s): %s, head is #%d...", #q,            \
                  dres_name(__item, buf, sizeof(buf)), __h);    \
            h##q = __h;                        \
            __item;                            \
        })

#define NEDGE(id) \
    (E + DRES_INDEX(id) + (DRES_ID_TYPE(id) == DRES_TYPE_VARIABLE ? ntarget:0))

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
    memset(E, 0           ,         sizeof(*E));
    
    hL = tL = hQ = tQ = 0;

    /* initialize L, incoming edges / node */
    status = 0;
    for (i = 0; i < nvariable; i++) {
        prq = graph->depends + graph->ntarget + i;
        if (prq->nid == -1)                     /* not in the graph at all */
            continue;
        
        if (variables[i].id != DRES_VARIABLE(i)) {
            DEBUG("##### %s@%s:%d: BUG, unexpected variable ID:",
                  __FUNCTION__, __FILE__, __LINE__);
            DEBUG("##### 0x%x != 0x%x", variables[i].id, DRES_VARIABLE(i));
            status = EINVAL;
        }
        
        PUSH(Q, variables[i].id);      /* variables do not depend on anything */
        
        for (j = 0; j < prq->nid; j++) {
            
            DEBUG("edge %s -> %s",
                  dres_name(DRES_VARIABLE(i), buf, sizeof(buf)),
                  dres_name(prq->ids[j], buf1, sizeof(buf1)));
            
            if (*NEDGE(prq->ids[j]) < 0)
                *NEDGE(prq->ids[j]) = 1;
            else
                *NEDGE(prq->ids[j]) += 1;
        }
        
    }
    
    for (i = 0; i < ntarget; i++) {
        dres_target_t *t;

        prq = graph->depends + i;
        t   = targets + i;
        
        if (prq->nid == -1)                     /* not in the graph at all */
            continue;
        
        DEBUG("checking target #%d (%s)...", i,
              dres_name(DRES_TARGET(i), buf, sizeof(buf)));

        if (targets[i].id != DRES_TARGET(i)) {
            DEBUG("### %s@%s:%d: BUG, unexpected target ID:",
                  __FUNCTION__, __FILE__, __LINE__);
            DEBUG("### 0x%x != 0x%x", targets[i].id, DRES_TARGET(i));
            status = EINVAL;
        }

        if (t->prereqs->nid == 0)
            PUSH(Q, t->id);
        
        for (j = 0; j < prq->nid; j++) {
            DEBUG("edge %s -> %s",
                  dres_name(DRES_TARGET(i), buf, sizeof(buf)),
                  dres_name(prq->ids[j], buf1, sizeof(buf1)));
                   
            if (*NEDGE(prq->ids[j]) < 0)
                *NEDGE(prq->ids[j]) = 1;
            else
                *NEDGE(prq->ids[j]) += 1;
        }
    }

    
    for (i = 0; i < ntarget; i++)
        DEBUG("E[%s] = %d", dres_name(targets[i].id, buf, sizeof(buf)),
              *NEDGE(targets[i].id));
    
    for (i = 0; i < nvariable; i++)
        DEBUG("E[%s] = %d", dres_name(variables[i].id, buf, sizeof(buf)),
              *NEDGE(variables[i].id));
    
    
    /* try to sort topologically the graph */
    hQ = hL = 0;
    while ((node = POP(Q)) != DRES_ID_NONE) {
        PUSH(L, node);
        prq = graph->depends + PRQ_IDX(node);
        for (i = 0; i < prq->nid; i++) {
            if (!DRES_IS_DELETED(prq->ids[i])) {
                DEBUG("  DELETE edge %s -> %s",
                      dres_name(node, buf, sizeof(buf)),
                      dres_name(prq->ids[i], buf1, sizeof(buf1)));
                prq->ids[i] = DRES_DELETED(prq->ids[i]);
                if (*NEDGE(prq->ids[i]) == 1) {
                    *NEDGE(prq->ids[i]) = 0;
                    PUSH(Q, prq->ids[i]);
                }
                else
                    *NEDGE(prq->ids[i]) -= 1;
                
                DEBUG("  # of edges to %s: %d",
                      dres_name(prq->ids[i], buf, sizeof(buf)),
                      *NEDGE(prq->ids[i]));
            }
            else {
                DEBUG("  edge %s -> %s already deleted",
                      dres_name(node, buf, sizeof(buf)),
                      dres_name(prq->ids[i], buf1, sizeof(buf1)));
                      
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
    

    if (status != 0)
        goto fail;

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
 * dres_dump_sort
 ********************/
void
dres_dump_sort(int *list)
{
    int  i;
    char buf[32];
   
    for (i = 0; list[i] != DRES_ID_NONE; i++)
        printf("  #%03d: 0x%x (%s)\n", i, list[i],
               dres_name(list[i], buf, sizeof(buf)));
}



/********************
 * dres_update_goal
 ********************/
int
dres_update_goal(char *goal)
{
    dres_graph_t  *graph;
    dres_target_t *target;
    int           *list, id, i;

    graph = NULL;
    list  = NULL;

    if ((target = dres_lookup_target(goal)) == NULL)
        goto fail;
    
    if (!DRES_IS_DEFINED(target->id))
        goto fail;
    
    if ((graph = dres_build_graph(goal)) == NULL)
        goto fail;
    
    if ((list = dres_sort_graph(graph)) == NULL)
        goto fail;

    printf("topological sort for goal %s:\n", goal);
    dres_dump_sort(list);
    
    stamp++;

    for (i = 0; list[i] != DRES_ID_NONE; i++) {
        id = list[i];

        if (DRES_ID_TYPE(id) != DRES_TYPE_TARGET)
            continue;
        
        check_target(id);
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
check_variable(int id, int refstamp)
{
    dres_variable_t *var = variables + DRES_INDEX(id);
    char             buf[32];
    
    DEBUG("should check %s...", dres_name(id, buf, sizeof(buf)));
    
#ifdef MEGA_TEST_HACK
    if (!strcmp(var->name, "sleeping_request") ||
        !strcmp(var->name, "idle_time"))
        return FALSE;
#endif

    var->stamp = stamp;               /* fake that variables has changed */
    
    return var->stamp >= refstamp;    /* XXX: >= or is > enough ? */
}


/********************
 * check_target
 ********************/
static int
check_target(int tid)
{
    dres_target_t *target, *t;
    dres_prereq_t *prq;
    int            i, update, id;
    char           buf[32];

    DEBUG("checking %s...", dres_name(tid, buf, sizeof(buf)));

    target = targets + DRES_INDEX(tid);
    update = FALSE;
    
    if ((prq = target->prereqs) == NULL)
        update = TRUE;
    else {
        for (i = 0; i < prq->nid; i++) {
            id = prq->ids[i];
            switch (DRES_ID_TYPE(id)) {
            case DRES_TYPE_VARIABLE:
                update |= check_variable(id, target->stamp);
                break;
            case DRES_TYPE_TARGET:
                t = targets + DRES_INDEX(id);
                update |= (t->stamp >= target->stamp);
                break;
            default:
                DEBUG("### BUG: invalid prereq 0x%x for %s", id, target->name);
                break;
            }
        }
    }
        
    if (update) {
        execute_actions(target);
        target->stamp = stamp;
    }
    
    return update;
}


/********************
 * execute_actions
 ********************/
static int
execute_actions(dres_target_t *target)
{
    dres_action_t *a;
    int            i;
    char           buf[32], *t;

    if (target->actions == NULL)
        return 0;

    DEBUG("executing actions for %s", target->name);

    for (a = target->actions; a; a = a->next) {
        printf("[%s]     %s(", __FUNCTION__, a->name);
        for (i = 0, t = ""; i < a->nargument; i++, t=",")
            printf("%s%s", t, dres_name(a->arguments[i], buf, sizeof(buf)));
        printf(")\n");
    }
    
    return 0;
}







#if 0

/********************
 * update_target
 ********************/
int
update_target(dres_target_t *target)
{
    dres_target_t   *t;
    dres_variable_t *v;
    dres_prereq_t   *prq;

    int i, id, update = 0;
    
    if ((prq = target->prereq) == NULL)
        update = TRUE;
    else {
        for (i = 0; i < prq->nid; i++) {
            id = prq->ids[i];
            switch (DRES_ID_TYPE(id)) {
            case DRES_TYPE_VARIABLE:
                update |= check_variable(id, target->stamp);
                break;
            case DRES_TYPE_TARGET:
                update |= check_target(id, target->stamp);
                break;
            default:
                DEBUG("### BUG: invalid prereq 0x%x for %s", target->name);
                break;
            }
        }
    }

    if (update) {
        execute_actions(target);
    }    
}

#endif


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
