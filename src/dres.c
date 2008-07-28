#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <ohm/ohm-fact.h>              /* XXX <ohm/ohm-fact.h> */

#include <dres/dres.h>
#include <dres/compiler.h>

#include "dres-debug.h"
#include "parser.h"


/* trace flags */
int DBG_GRAPH, DBG_VAR, DBG_RESOLVE;

TRACE_DECLARE_COMPONENT(trcdres, "dres",
    TRACE_FLAG_INIT("graph"  , "dependency graph"    , &DBG_GRAPH),
    TRACE_FLAG_INIT("var"    , "variable handling"   , &DBG_VAR),
    TRACE_FLAG_INIT("resolve", "dependency resolving", &DBG_RESOLVE));
    

extern FILE *yyin;
extern int   yyparse(dres_t *dres);

static int  finalize_variables(dres_t *dres);
static int  finalize_actions(dres_t *dres);

static void transaction_commit  (dres_t *dres);
static void transaction_rollback(dres_t *dres);



int depth = 0;


/********************
 * dres_init
 ********************/
EXPORTED dres_t *
dres_init(char *prefix)
{
    dres_t *dres;
    int     status;
    
    trace_init();
    trace_add_component(NULL, &trcdres);
    
    if (ALLOC_OBJ(dres) == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    
    dres->fact_store = dres_store_init(STORE_FACT,prefix,dres_update_var_stamp);
    dres->dres_store = dres_store_init(STORE_LOCAL, NULL, NULL);

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
EXPORTED void
dres_exit(dres_t *dres)
{
    if (dres == NULL)
        return;
    
    dres_free_targets(dres);
    dres_free_factvars(dres);
    dres_free_dresvars(dres);
    dres_free_literals(dres);
    
    dres_store_destroy(dres->fact_store);
    dres_store_destroy(dres->dres_store);

    FREE(dres);
}


/********************
 * dres_parse_file
 ********************/
EXPORTED int
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
 * dres_set_prefix
 ********************/
EXPORTED int
dres_set_prefix(dres_t *dres, char *prefix)
{
    if (!dres_store_set_prefix(dres->fact_store, prefix))
        return errno ?: ENOMEM;
    else
        return 0;
}

/********************
 * dres_get_prefix
 ********************/
EXPORTED char *
dres_get_prefix(dres_t *dres)
{
    return dres_store_get_prefix(dres->fact_store);
}


/********************
 * dres_check_stores
 ********************/
void
dres_check_stores(dres_t *dres)
{
    dres_variable_t *var;
    unsigned int              i;
    char             name[128];

    for (i = 0, var = dres->factvars; i < dres->nfactvar; i++, var++) {
        sprintf(name, "%s%s", dres_get_prefix(dres), var->name);
        if (!dres_store_check(dres->fact_store, name))
            DEBUG(DBG_VAR, "lookup of %s FAILED", name);
    }
}


/********************
 * finalize_variables
 ********************/
static int
finalize_variables(dres_t *dres)
{
    dres_variable_t *v;
    unsigned int              i, monitor;

    for (i = 0, v = dres->factvars; i < dres->nfactvar; i++, v++) {
        monitor = DRES_TST_FLAG(v, VAR_PREREQ);
        if (!(v->var = dres_var_init(dres->fact_store, v->name, v, monitor)))
            return EIO;
    }
    
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
    void           *unknown = dres_lookup_handler(dres, DRES_BUILTIN_UNKNOWN);
    unsigned int    i;
    int             status;
    
    status = 0;
    for (i = 0, target = dres->targets; i < dres->ntarget; i++, target++)
        for (action = target->actions; action; action = action->next)
            if (!(action->handler = dres_lookup_handler(dres, action->name))) {
                action->handler = unknown;
                status = ENOENT;
            }

    DRES_SET_FLAG(dres, ACTIONS_FINALIZED);
    return 0;
}


/********************
 * finalize_targets
 ********************/
static int
finalize_targets(dres_t *dres)
{
    dres_target_t *target;
    dres_graph_t  *graph;
    char           goal[64];
    unsigned int            i;

    for (i = 0, target = dres->targets; i < dres->ntarget; i++, target++) {
        dres_name(dres, target->id, goal, sizeof(goal));

        if ((graph = dres_build_graph(dres, target)) == NULL)
            return EINVAL;
        
        target->dependencies = dres_sort_graph(dres, graph);
        dres_free_graph(graph);

        if (target->dependencies == NULL)
            return EINVAL;

        DEBUG(DBG_GRAPH, "topological sort for goal %s:\n", goal);
        dres_dump_sort(dres, target->dependencies);
    }

    DRES_SET_FLAG(dres, TARGETS_FINALIZED);
    return 0;
}


/********************
 * dres_finalize
 ********************/
EXPORTED int
dres_finalize(dres_t *dres)
{
    int status;
    
    if ((status = finalize_actions(dres)) || (status = finalize_targets(dres)))
        return status;
    else
        return 0;
}


/********************
 * dres_update_goal
 ********************/
EXPORTED int
dres_update_goal(dres_t *dres, char *goal, char **locals)
{
    dres_target_t *target;
    int            id, i, own_tx;
    int            status = 0;

    if (!DRES_TST_FLAG(dres, ACTIONS_FINALIZED))
        if ((status = finalize_actions(dres)) != 0)
            if (dres->fallback.handler == NULL)
                return status;
    
    if (!DRES_TST_FLAG(dres, TARGETS_FINALIZED))
        if ((status = finalize_targets(dres)) != 0)
            return status;
    
    if ((target = dres_lookup_target(dres, goal)) == NULL)
        return EINVAL;
    
    if (!DRES_IS_DEFINED(target->id))
        return EINVAL;

    if (!DRES_TST_FLAG(dres, TRANSACTION_ACTIVE)) {
        if (!dres_store_tx_new(dres->fact_store))
            return EINVAL;
        dres->txid++;
        own_tx = 1;
    }

    dres->stamp++;
    dres_store_update_timestamps(dres->fact_store, dres);
    
    if (locals != NULL && (status = dres_scope_push_args(dres, locals)) != 0)
        goto rollback;
    
    if (target->prereqs == NULL) {
        DEBUG(DBG_RESOLVE, "%s has no prereqs => updating", target->name);
        status = dres_run_actions(dres, target);
    }
    else {
        for (i = 0; target->dependencies[i] != DRES_ID_NONE; i++) {
            id = target->dependencies[i];
        
            if (DRES_ID_TYPE(id) != DRES_TYPE_TARGET)
                continue;
        
            if ((status = dres_check_target(dres, id)) != 0)
                break;
        }
    }
    
    if (locals != NULL)
        dres_scope_pop(dres);
    
    if (status == 0) {
        dres_update_target_stamp(dres, target);
        if (own_tx)
            transaction_commit(dres);
    }
    else {
    rollback:
        if (own_tx)
            transaction_rollback(dres);
    }
    
    return status;
}


/********************
 * dres_lookup_variable
 ********************/
EXPORTED dres_variable_t *
dres_lookup_variable(dres_t *dres, int id)
{
    unsigned int idx = DRES_INDEX(id);
    
    switch (DRES_ID_TYPE(id)) {
    case DRES_TYPE_FACTVAR:
        return idx > dres->nfactvar ? NULL : dres->factvars + idx;
    case DRES_TYPE_DRESVAR:
        return idx > dres->ndresvar ? NULL : dres->dresvars + idx;
    }        

    return NULL;
}


/********************
 * transaction_commit
 ********************/
static void
transaction_commit(dres_t *dres)
{
    dres_store_tx_commit(dres->fact_store);
    DRES_CLR_FLAG(dres, TRANSACTION_ACTIVE);
}


/********************
 * transaction_rollback
 ********************/
static void
transaction_rollback(dres_t *dres)
{
    dres_target_t   *t;
    dres_variable_t *var;
    unsigned int     i;

    dres_store_tx_rollback(dres->fact_store);
    DRES_CLR_FLAG(dres, TRANSACTION_ACTIVE);

    for (i = 0, t = dres->targets; i < dres->ntarget; i++, t++)
        if (t->txid == dres->txid)
            t->stamp = t->txstamp;

    for (i = 0, var = dres->dresvars; i < dres->ndresvar; i++, var++)
        if (var->txid == dres->txid)
            var->stamp = var->txstamp;
}


/********************
 * dres_update_var_stamp
 ********************/
void
dres_update_var_stamp(void *dresp, void *varp)
{
    dres_t          *dres = (dres_t *)dresp;
    dres_variable_t *var  = (dres_variable_t *)varp;
    
    if (var->txid != dres->txid) {
        var->txid    = dres->txid;
        var->txstamp = var->stamp;
    }
    var->stamp = dres->stamp;
}


/********************
 * dres_update_target_stamp
 ********************/
void
dres_update_target_stamp(dres_t *dres, dres_target_t *target)
{
    if (target->txid != dres->txid) {
        target->txid    = dres->txid;
        target->txstamp = target->stamp;
    }
    target->stamp = dres->stamp;
}




/*****************************************************************************
 *                       *** misc. dumping/debugging routines                *
 *****************************************************************************/


/********************
 * dres_name
 ********************/
EXPORTED char *
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
        
    case DRES_TYPE_FACTVAR:
        variable = dres->factvars + DRES_INDEX(id);
        snprintf(buf, bufsize, "$%s", variable->name);
        break;

    case DRES_TYPE_DRESVAR:
        variable = dres->dresvars + DRES_INDEX(id);
        snprintf(buf, bufsize, "&%s", variable->name);
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
 * dres_dump_varref
 ********************/
char *
dres_dump_varref(dres_t *dres, char *buf, size_t bufsize, dres_varref_t *vr)
{
    int len;

    if (vr == NULL)
        return NULL;

    if (vr->variable == DRES_ID_NONE)
        return NULL;
    
    dres_name(dres, vr->variable, buf, bufsize);
    len = strlen(buf);
    
    snprintf(buf + len, bufsize - len, "%s%s%s%s%s",
             vr->selector ? "[" : "",
             vr->selector ?: "",
             vr->selector ? "]" : "",
             vr->field ? ":" : "", vr->field ?: "");
    
    return buf;
}


/********************
 * dres_dump_sort
 ********************/
EXPORTED void
dres_dump_sort(dres_t *dres, int *list)
{
    int  i;
    char buf[32];
   
    for (i = 0; list[i] != DRES_ID_NONE; i++)
        DEBUG(DBG_GRAPH, "  #%03d: 0x%x (%s)\n", i, list[i],
              dres_name(dres, list[i], buf, sizeof(buf)));
}


/********************
 * yyerror
 ********************/
EXPORTED void
yyerror(dres_t *dres, const char *msg)
{
    extern int lineno;
    extern void dres_parse_error(dres_t *, int, const char *, const char *);

    dres_parse_error(dres, lineno, msg, yylval.string);
}





/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
