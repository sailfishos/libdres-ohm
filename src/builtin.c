#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <prolog/ohm-fact.h>
#include <dres/dres.h>

static int dres_builtin_assign(dres_t *dres,
                               char *name, dres_action_t *action, void **ret);
static int dres_builtin_dres  (dres_t *dres,
                               char *name, dres_action_t *action, void **ret);
static int dres_builtin_resolve(dres_t *dres,
                                char *name, dres_action_t *action, void **ret);
static int dres_builtin_shell (dres_t *dres,
                               char *name, dres_action_t *action, void **ret);

static int dres_builtin_unknown(dres_t *dres,
                                char *name, dres_action_t *action, void **ret);



/*****************************************************************************
 *                            *** builtin handlers ***                       *
 *****************************************************************************/

/********************
 * dres_register_builtins
 ********************/
int
dres_register_builtins(dres_t *dres)
{
#define BUILTIN(n) { .name = #n, .handler = dres_builtin_##n }
    dres_handler_t builtins[] = {
        { .name = DRES_BUILTIN_ASSIGN, .handler = dres_builtin_assign },
        BUILTIN(dres),
        BUILTIN(resolve),
        BUILTIN(shell),
        { .name = NULL }
    }, *h;
#undef BUILTIN

    int status;

    for (h = builtins; h->name; h++)
        if ((status = dres_register_handler(dres, h->name, h->handler)) != 0)
            return status;

    return 0;
}


/********************
 * dres_builtin_assign
 ********************/
static int
dres_builtin_assign(dres_t *dres, char *act, dres_action_t *action, void **ret)
{
    OhmFactStore *store = ohm_fact_store_get_fact_store();
    GSList       *list;
    
    char      *prefix;
    char      name[64], factname[64];
    OhmFact **facts;
    int       nfact, i;
    
    if (DRES_ID_TYPE(action->lvalue.variable) != DRES_TYPE_FACTVAR || !ret)
        return EINVAL;
    
    if (action->immediate != DRES_ID_NONE) {
        *ret = NULL;
        return 0;              /* handled in action.c:assign_result */
    }
    
    prefix = dres_get_prefix(dres);
#if 1
    dres_name(dres, action->rvalue.variable, name, sizeof(name));
#else
    dres_name(dres, action->lvalue.variable, name, sizeof(name));
#endif
    snprintf(factname, sizeof(factname), "%s%s", prefix, name + 1);
    
    if ((list = ohm_fact_store_get_facts_by_name(store, factname)) != NULL) {
        nfact = g_slist_length(list);
        if ((facts = ALLOC_ARR(OhmFact *, nfact + 1)) == NULL)
            return ENOMEM;
        for (i = 0; i < nfact && list != NULL; i++, list = g_slist_next(list))
            facts[i] = g_object_ref((OhmFact *)list->data);
        facts[i] = NULL;
    }
    
    *ret = facts;
    return 0;
}


/********************
 * dres_builtin_dres
 ********************/
static int
dres_builtin_dres(dres_t *dres, char *name, dres_action_t *action, void **ret)
{
    char goal[64];
    int  status;
    
    /* XXX TODO: factstore transaction */
    
    if (action->arguments == NULL)
        return EINVAL;
    
    dres_name(dres, action->arguments[0], goal, sizeof(goal));
    
    DEBUG("DRES recursing for goal %s", goal);
    depth++;
    dres_scope_push(dres, action->variables, action->nvariable);
    status = dres_update_goal(dres, goal, NULL);
    dres_scope_pop(dres);
    depth--;
    DEBUG("DRES back from goal %s", goal);

    *ret = NULL;
    return status;
}


/********************
 * dres_builtin_resolve
 ********************/
static int
dres_builtin_resolve(dres_t *dres,
                     char *name, dres_action_t *action, void **ret)
{
    return dres_builtin_dres(dres, name, action, ret);
}




/********************
 * dres_builtin_shell
 ********************/
static int
dres_builtin_shell(dres_t *dres, char *name, dres_action_t *action, void **ret)
{
    return dres_builtin_unknown(dres, name, action, ret);
}


/********************
 * dres_builtin_unknown
 ********************/
static int
dres_builtin_unknown(dres_t *dres,
                     char *name, dres_action_t *action, void **ret)
{
    if (action == NULL)
        return 0;

    DEBUG("unknown action %s", name);
    
    printf("*** unknown action %s", name);
    dres_dump_action(dres, action);

    return 0;
}





/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
