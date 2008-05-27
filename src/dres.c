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

static int  finalize_variables(dres_t *dres);
static int  finalize_actions(dres_t *dres);


int depth = 0;


/********************
 * dres_init
 ********************/
dres_t *
dres_init(char *prefix)
{
    dres_t *dres;
    int     status;

    if (ALLOC_OBJ(dres) == NULL) {
        errno = ENOMEM;
        return NULL;
    }
    
    dres->fact_store = dres_store_init(STORE_FACT , prefix);
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
 * dres_set_prefix
 ********************/
int
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
char *
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
    int              i;
    char             name[128];

    for (i = 0, var = dres->factvars; i < dres->nfactvar; i++, var++) {
        sprintf(name, "%s%s", dres_get_prefix(dres), var->name);
        if (!dres_store_check(dres->fact_store, name))
            DEBUG("*** lookup of %s FAILED", name);
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

    for (i = 0, v = dres->factvars; i < dres->nfactvar; i++, v++)
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



/********************
 * dres_update_goal
 ********************/
int
dres_update_goal(dres_t *dres, char *goal, char **locals)
{
    dres_graph_t  *graph;
    dres_target_t *target;
    int           *list, id, i, status;

    graph = NULL;
    list  = NULL;

    if (!DRES_TST_FLAG(dres, ACTIONS_FINALIZED))
        if ((status = finalize_actions(dres)) != 0)
            return EINVAL;

    dres_store_update_timestamps(dres->fact_store, ++(dres->stamp));

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

    if (locals != NULL && dres_scope_push_args(dres, locals) != 0)
        goto fail;
    
    for (i = 0; list[i] != DRES_ID_NONE; i++) {
        id = list[i];

        if (DRES_ID_TYPE(id) != DRES_TYPE_TARGET)
            continue;
        
        dres_check_target(dres, id);
    }

    if (locals != NULL)
        dres_scope_pop(dres);

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
 * dres_lookup_variable
 ********************/
dres_variable_t *
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
    
    snprintf(buf + len, bufsize - len, "%s%s%s",
             vr->selector ? "[" : "",
             vr->selector ?: "",
             vr->selector ? "]" : "",
             vr->field ? ":" : "", vr->field ?: "");
    
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
    extern int lineno;

    dres_parse_error(dres, lineno, msg, yylval.string);
}





/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
