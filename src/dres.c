#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <ohm/ohm-fact.h>

#include <dres/dres.h>
#include <dres/compiler.h>

#include "dres-debug.h"
#include "parser-types.h"
#include "parser.h"


/* trace flags */
int DBG_GRAPH, DBG_VAR, DBG_RESOLVE;

TRACE_DECLARE_COMPONENT(trcdres, "dres",
    TRACE_FLAG_INIT("graph"  , "dependency graph"    , &DBG_GRAPH),
    TRACE_FLAG_INIT("var"    , "variable handling"   , &DBG_VAR),
    TRACE_FLAG_INIT("resolve", "dependency resolving", &DBG_RESOLVE));
    

extern FILE *yyin;
extern int   yyparse(dres_t *dres);

static int  initialize_variables(dres_t *dres);
static int  finalize_variables  (dres_t *dres);
static int  finalize_actions    (dres_t *dres);

static int  push_locals(dres_t *dres, char **locals);
static int  pop_locals (dres_t *dres);

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

    if (vm_init(&dres->vm, 32))
        goto fail;

    dres->fact_store = dres_store_init(STORE_FACT,prefix,dres_update_var_stamp);

    if ((status = dres_register_builtins(dres)) != 0)
        goto fail;
    
    if (dres->fact_store == NULL)
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
    
    dres_store_destroy(dres->fact_store);

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
        status = initialize_variables(dres);
    if (status == 0)
        status = finalize_variables(dres);

    dres->vm.nlocal = dres->ndresvar;
    
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
    
    for (i = 0, var = dres->factvars; i < dres->nfactvar; i++, var++) {
        if (!dres_store_check(dres->fact_store, var->name + 1))
            DEBUG(DBG_VAR, "lookup of %s FAILED", var->name + 1);
    }
}


/********************
 * create_variable
 ********************/
static int
create_variable(dres_t *dres, char *name, dres_init_t *fields)
{
    dres_init_t  *init;
    char         *field;
    dres_value_t *value;
    OhmFactStore *store = ohm_fact_store_get_fact_store();
    OhmFact      *fact;
    GValue       *gval;

    if (store == NULL)
        return EINVAL;

    if ((fact = ohm_fact_new(name)) == NULL)
        return ENOMEM;

    for (init = fields; init != NULL; init = init->next) {
        field = init->field.name;
        value = &init->field.value;
        switch (value->type) {
        case DRES_TYPE_INTEGER: gval = ohm_value_from_int(value->v.i);    break;
        case DRES_TYPE_DOUBLE:  gval = ohm_value_from_double(value->v.d); break;
        case DRES_TYPE_STRING:  gval = ohm_value_from_string(value->v.s); break;
        default:                return EINVAL;
        }

        ohm_fact_set(fact, field, gval);
    }

    if (!ohm_fact_store_insert(store, fact))
        return EINVAL;

    return 0;

    (void)dres;
}


/********************
 * initialize_variables
 ********************/
static int
initialize_variables(dres_t *dres)
{
    dres_initializer_t *init;
    char                name[128];
    int                 status;

    for (init = dres->initializers; init != NULL; init = init->next) {
        dres_name(dres, init->variable, name, sizeof(name));
        if ((status = create_variable(dres, name + 1, init->fields)) != 0)
            return status;
    }
    
    return 0;
}


/********************
 * finalize_variables
 ********************/
static int
finalize_variables(dres_t *dres)
{
    dres_variable_t *v;
    int              i, monitor;

    for (i = 0, v = dres->factvars; i < dres->nfactvar; i++, v++) {
        monitor = DRES_TST_FLAG(v, VAR_PREREQ);
        if (!(v->var = dres_var_init(dres->fact_store, v->name, v, monitor)))
            return EIO;
    }
    
    dres_store_finish(dres->fact_store);

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
    dres_call_t    *call;
    void           *unknown = dres_lookup_handler(dres, DRES_BUILTIN_UNKNOWN);
    int             i, status;
    
    status = 0;
    for (i = 0, target = dres->targets; i < dres->ntarget; i++, target++) {
        for (action = target->actions; action; action = action->next) {
            if (action->type != DRES_ACTION_CALL)
                continue;
            call = action->call;
            if (!(call->handler = dres_lookup_handler(dres, call->name))) {
                call->handler = unknown;
                status = ENOENT;
            }
        }
        if ((status = dres_compile_target(dres, target)) != 0)
            return status;
    }

    printf("*** succefully compiled all targets\n");

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
    int            i;

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
#define ROLLBACK(ec)  do { status = (ec); goto rollback; } while (0)
#define POPLOCALS(ec) do { status = (ec); goto pop; } while (0)

    dres_target_t *target;
    int            id, i, status, own_tx;

    
    status = 0;

    if (!DRES_TST_FLAG(dres, ACTIONS_FINALIZED))
        if ((status = finalize_actions(dres)) != 0)
            if (dres->fallback == NULL)
                return status;
    
    if (!DRES_TST_FLAG(dres, TARGETS_FINALIZED))
        if ((status = finalize_targets(dres)) != 0)
            return status;
    
    if (goal != NULL) {
        if ((target = dres_lookup_target(dres, goal)) == NULL)
            return EINVAL;
    }
    else {
        target = dres->targets;
        goal   = target->name;
    }
    
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
    
    if (locals != NULL && (status = push_locals(dres, locals)) != 0)
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
        pop_locals(dres);
    
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
    int idx = DRES_INDEX(id);
    
    switch (DRES_ID_TYPE(id)) {
    case DRES_TYPE_FACTVAR:
        return idx > dres->nfactvar ? NULL : dres->factvars + idx;
    case DRES_TYPE_DRESVAR:
        return idx > dres->ndresvar ? NULL : dres->dresvars + idx;
    }        

    return NULL;
}


/********************
 * push_locals
 ********************/
static int
push_locals(dres_t *dres, char **locals)
{
#define FAIL(ec) do { err = (ec); goto fail; } while (0)
    vm_value_t v;
    int        err, id, i;
    
    if (locals == NULL)
        return 0;
    
    if ((err = vm_scope_push(&dres->vm)) != 0)
        return err;
    
    for (i = 0; locals[i] != NULL; i += 2) {
        id  = dres_dresvar_id(dres, locals[i]);
        v.s = locals[i+1];

        if (v.s == NULL)
            FAIL(EINVAL);
            
        if (id == DRES_ID_NONE) {
            printf("*** cannot set unknown &%s to \"%s\"\n", locals[i], v.s);
            FAIL(ENOENT);
        }
            
        if ((err = vm_scope_set(&dres->vm.scope, id, DRES_TYPE_STRING, v)) != 0)
            FAIL(err);
    }
    
    
    return 0;

 fail:
    vm_scope_pop(&dres->vm);
    return err;
}


/********************
 * pop_locals
 ********************/
static int
pop_locals(dres_t *dres)
{
    return vm_scope_pop(&dres->vm);
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
    int              i;

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
    default:
        snprintf(buf, bufsize, "<invalid id 0x%x>", id);
    }

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
