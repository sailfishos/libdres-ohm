/******************************************************************************/
/*  This file is part of dres the resource policy dependency resolver.        */
/*                                                                            */
/*  Copyright (C) 2010 Nokia Corporation.                                     */
/*                                                                            */
/*  This library is free software; you can redistribute                       */
/*  it and/or modify it under the terms of the GNU Lesser General Public      */
/*  License as published by the Free Software Foundation                      */
/*  version 2.1 of the License.                                               */
/*                                                                            */
/*  This library is distributed in the hope that it will be useful,           */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU          */
/*  Lesser General Public License for more details.                           */
/*                                                                            */
/*  You should have received a copy of the GNU Lesser General Public          */
/*  License along with this library; if not, write to the Free Software       */
/*  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  */
/*  USA.                                                                      */
/******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <dres/dres.h>
#include "dres-debug.h"

static int graph_build_prereq(dres_t *dres, dres_graph_t *graph,
                              dres_target_t *target, int prereq);
static int graph_has_prereq(dres_graph_t *graph, int tid, int prid);
static int graph_add_prereq(dres_t *dres, dres_graph_t *graph,int tid,int prid);
static int graph_add_leafs(dres_t *dres, dres_graph_t *graph);



/*****************************************************************************
 *                        *** dependency graph handling ***                  *
 *****************************************************************************/


/********************
 * dres_build_graph
 ********************/
dres_graph_t *
dres_build_graph(dres_t *dres, dres_target_t *target)
{
    dres_graph_t *graph;
    int           prid, i, n;

    graph = NULL;

    if (!DRES_IS_DEFINED(target->id))
        goto fail;
    
    if (ALLOC_OBJ(graph) == NULL)
        goto fail;
    
    n = dres->ntarget + dres->nfactvar + dres->ndresvar;
    
    if ((graph->depends = ALLOC_ARR(typeof(*graph->depends), n)) == NULL)
        goto fail;

    for (i = 0; i < n; i++)
        graph->depends[i].nid = -1;                /* node not in graph yet */

    graph->ntarget  = dres->ntarget;
    graph->nfactvar = dres->nfactvar;
    graph->ndresvar = dres->ndresvar;
    
    if (target->prereqs != NULL) {
        for (i = 0; i < target->prereqs->nid; i++) {
            prid = target->prereqs->ids[i];
            graph_build_prereq(dres, graph, target, prid);
        }
    }
    

    /*
     * make sure targets that have only prerequisites but are not
     * prerequisites themselves (eg. our goal) are also part of the graph
     *
     * XXX Is this really needed ? I think it'd be enough to add just our
     * XXX goal...
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
    int            i, n;
    
    if (graph == NULL || graph->depends == NULL)
        return;
    
    n = graph->ntarget + graph->nfactvar + graph->ndresvar;
    for (i = 0; i < n; i++) {
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

    DEBUG(DBG_GRAPH, "0x%x (%s) -> %s",
          prereq, dres_name(dres, prereq, name,sizeof(name)), target->name);
    
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

    case DRES_TYPE_FACTVAR:
    case DRES_TYPE_DRESVAR:
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

    idx = DRES_INDEX(prid);

    switch (DRES_ID_TYPE(prid)) {
    case DRES_TYPE_DRESVAR: idx += graph->nfactvar; /* fall through */
    case DRES_TYPE_FACTVAR: idx += graph->ntarget;  /* fall through */
    case DRES_TYPE_TARGET:  break;
    }

    depends = graph->depends + idx;
    
    if (depends->nid < 0)
        depends->nid = 0;                          /* unmark as not present */

    if (!REALLOC_ARR(depends->ids, depends->nid, depends->nid + 1))
        return ENOMEM;

    depends->ids[depends->nid++] = tid;
    return 0;

    (void)dres;
}


/********************
 * graph_has_prereq
 ********************/
static int
graph_has_prereq(dres_graph_t *graph, int tid, int prid)
{
    dres_prereq_t *prereqs;
    int            idx, i;

    idx = DRES_INDEX(prid);
    
    switch (DRES_ID_TYPE(prid)) {
    case DRES_TYPE_DRESVAR: idx += graph->nfactvar; /* fall through */
    case DRES_TYPE_FACTVAR: idx += graph->ntarget;  /* fall through */
    case DRES_TYPE_TARGET:  break;
    }
    
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
            

    /* check targets */
    for (i = 0; i < graph->ntarget; i++) {
        prq = graph->depends + i;
        for (j = 0; j < prq->nid; j++) {
            id = prq->ids[j];
            if (DRES_ID_TYPE(id) == DRES_TYPE_TARGET) {
                target = graph->depends + DRES_INDEX(id);
                if (target->nid < 0) {
                    target->nid = 0;            /* unmark as not present */
                    DEBUG(DBG_GRAPH, "leaf target %s (0x%x) pulled in",
                          dres_name(dres, id, buf, sizeof(buf)), id);
                }
            }
        }
    }


    /* check factvars */
    for (i = graph->ntarget; i < graph->nfactvar; i++) {
        prq = graph->depends + graph->ntarget + i;
        for (j = 0; j < prq->nid; j++) {
            id = prq->ids[j];
            if (DRES_ID_TYPE(id) == DRES_TYPE_TARGET) {
                target = graph->depends + DRES_INDEX(id);
                if (target->nid < 0) {
                    target->nid = 0;              /* unmark as not present */
                    DEBUG(DBG_GRAPH, "leaf target %s (0x%x) pulled in",
                          dres_name(dres, id, buf, sizeof(buf)), id);
                }
            }
        }
    }

    /* check dresvars */
    for (i = graph->ntarget; i < graph->ndresvar; i++) {
        prq = graph->depends + graph->ntarget + graph->nfactvar + i;
        for (j = 0; j < prq->nid; j++) {
            id = prq->ids[j];
            if (DRES_ID_TYPE(id) == DRES_TYPE_TARGET) {
                target = graph->depends + DRES_INDEX(id);
                if (target->nid < 0) {
                    target->nid = 0;              /* unmark as not present */
                    DEBUG(DBG_GRAPH, "leaf target %s (0x%x) pulled in",
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
     *           return topologically sorted order: L
     */




#define PUSH(q, item) do {                                    \
        char buf[32];                                         \
                                                              \
        int __t = t##q;                                       \
        int __size = n;                                       \
                                                              \
        DEBUG(DBG_GRAPH, "PUSH(%s, %s), as item #%d...", #q,  \
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
            DEBUG(DBG_GRAPH, "POP(%s): %s, head is #%d...", #q,    \
                  dres_name(dres, __item, buf, sizeof(buf)), __h); \
            h##q = __h;                                            \
            __item;                                                \
        })


#define PRQ_IDX(id) ({                                                  \
            int __i = DRES_INDEX(id);                                   \
            switch (DRES_ID_TYPE(id)) {                                 \
            case DRES_TYPE_DRESVAR: __i += graph->nfactvar; /* fall through */ \
            case DRES_TYPE_FACTVAR: __i += graph->ntarget;  /* fall through */ \
            case DRES_TYPE_TARGET:  break;                                     \
            }                                                                  \
            __i;                                                        \
        })

#define NEDGE(id) (E + PRQ_IDX(id))


    int *L, *Q, *E;
    int  hL, hQ, tL, tQ;
    int  node, status;

    dres_prereq_t *prq;
    int            i, j, n;
    char           buf[32], buf1[32];
    

    L = Q = E = NULL;
    n = graph->ntarget + graph->nfactvar + graph->ndresvar;
    
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
    for (i = 0; i < graph->ndresvar; i++) {
        prq = graph->depends + graph->ntarget + graph->nfactvar + i;
        if (prq->nid == -1)                     /* not in the graph at all */
            continue;
        
        PUSH(Q, dres->dresvars[i].id); /* variables don't depend on anything */
        
        for (j = 0; j < prq->nid; j++) {
            DEBUG(DBG_GRAPH, "edge %s -> %s",
                  dres_name(dres, DRES_DRESVAR(i), buf, sizeof(buf)),
                  dres_name(dres, prq->ids[j], buf1, sizeof(buf1)));
            
            if (*NEDGE(prq->ids[j]) < 0)
                *NEDGE(prq->ids[j]) = 1;
            else
                *NEDGE(prq->ids[j]) += 1;
        }
    }

    for (i = 0; i < graph->nfactvar; i++) {
        prq = graph->depends + graph->ntarget + i;
        if (prq->nid == -1)                     /* not in the graph at all */
            continue;
        
        PUSH(Q, dres->factvars[i].id); /* variables don't depend on anything */
        
        for (j = 0; j < prq->nid; j++) {
            DEBUG(DBG_GRAPH, "edge %s -> %s",
                  dres_name(dres, DRES_FACTVAR(i), buf, sizeof(buf)),
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
        
        DEBUG(DBG_GRAPH, "checking target #%d (%s)...", i,
              dres_name(dres, DRES_TARGET(i), buf, sizeof(buf)));

#if 0 /* hmm... t->prereqs == NULL also indicates no incoming edges */
        if (t->prereqs != NULL && t->prereqs->nid == 0)
            PUSH(Q, t->id);
#else
        if (t->prereqs == NULL || t->prereqs->nid == 0)
            PUSH(Q, t->id);
#endif
        for (j = 0; j < prq->nid; j++) {
            DEBUG(DBG_GRAPH, "edge %s -> %s",
                  dres_name(dres, DRES_TARGET(i), buf, sizeof(buf)),
                  dres_name(dres, prq->ids[j], buf1, sizeof(buf1)));
                   
            if (*NEDGE(prq->ids[j]) < 0)
                *NEDGE(prq->ids[j]) = 1;
            else
                *NEDGE(prq->ids[j]) += 1;
        }
    }

    
    for (i = 0; i < dres->ntarget; i++)
        DEBUG(DBG_GRAPH, "E[%s] = %d",
              dres_name(dres, dres->targets[i].id, buf, sizeof(buf)),
              *NEDGE(dres->targets[i].id));
    
    for (i = 0; i < dres->nfactvar; i++)
        DEBUG(DBG_GRAPH, "E[%s] = %d",
              dres_name(dres, dres->factvars[i].id, buf, sizeof(buf)),
              *NEDGE(dres->factvars[i].id));

    for (i = 0; i < dres->ndresvar; i++)
        DEBUG(DBG_GRAPH, "E[%s] = %d",
              dres_name(dres, dres->dresvars[i].id, buf, sizeof(buf)),
              *NEDGE(dres->dresvars[i].id));
    
    
    /* try to sort topologically the graph */
    hQ = hL = 0;
    while ((node = POP(Q)) != DRES_ID_NONE) {
        PUSH(L, node);
        prq = graph->depends + PRQ_IDX(node);
        for (i = 0; i < prq->nid; i++) {
            if (!DRES_IS_DELETED(prq->ids[i])) {
                DEBUG(DBG_GRAPH, "  DELETE edge %s -> %s",
                      dres_name(dres, node, buf, sizeof(buf)),
                      dres_name(dres, prq->ids[i], buf1, sizeof(buf1)));
                prq->ids[i] = DRES_DELETED(prq->ids[i]);
                if (*NEDGE(prq->ids[i]) == 1) {
                    *NEDGE(prq->ids[i]) = 0;
                    PUSH(Q, prq->ids[i]);
                }
                else
                    *NEDGE(prq->ids[i]) -= 1;
                
                DEBUG(DBG_GRAPH, "  # of edges to %s: %d",
                      dres_name(dres, prq->ids[i], buf, sizeof(buf)),
                      *NEDGE(prq->ids[i]));
            }
            else {
                DEBUG(DBG_GRAPH, "  edge %s -> %s already deleted",
                      dres_name(dres, node, buf, sizeof(buf)),
                      dres_name(dres, prq->ids[i], buf1, sizeof(buf1)));
            }
        }
    }


    /* check that we exhausted all edges */
    for (i = 0; i < n; i++) {
        if (E[i] != 0) {
            DEBUG(DBG_GRAPH, "error: graph has cycles");
            DEBUG(DBG_GRAPH, "still has %d edges for %s #%d", E[i],
                  i < graph->ntarget ? "target" :
                  (i < graph->ntarget + graph->nfactvar ?
                   "FACT variable" : "DRES varariable"),
                  i < graph->ntarget ? i :
                  (i < graph->ntarget + graph->nfactvar ? i - graph->ntarget :
                   i - graph->ntarget - graph->nfactvar));
            status = EINVAL;
        }
    }
    
    
    if (status == 0) {
        free(Q);
        free(E);
        return L;
    }
    
    
 fail:
    if (L)
        free(L);
    if (Q)
        free(Q);
    if (E)
        free(E);
    
    return NULL;
}








/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
