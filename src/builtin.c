#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <ohm/ohm-fact.h>
#include <dres/dres.h>
#include "dres-debug.h"


#define BUILTIN_HANDLER(b)                                              \
    static int dres_builtin_##b(void *data, char *name,                 \
                                vm_stack_entry_t *args, int narg,       \
                                vm_stack_entry_t *rv)

#if 0
BUILTIN_HANDLER(assign);
#endif
BUILTIN_HANDLER(dres);
BUILTIN_HANDLER(resolve);
BUILTIN_HANDLER(echo);
BUILTIN_HANDLER(shell);
BUILTIN_HANDLER(fail);

#define BUILTIN(b) { .name = #b, .handler = dres_builtin_##b }

typedef struct dres_builtin_s {
    char           *name;
    dres_handler_t  handler;
} dres_builtin_t;

static dres_builtin_t builtins[] = {
#if 0
    { .name = DRES_BUILTIN_ASSIGN, .handler = dres_builtin_assign },
#endif
    BUILTIN(dres),
    BUILTIN(resolve),
    BUILTIN(echo),
    BUILTIN(shell),
    BUILTIN(fail),
    { .name = NULL, .handler = NULL }
};


/*****************************************************************************
 *                            *** builtin handlers ***                       *
 *****************************************************************************/

/********************
 * dres_fallback_call
 ********************/
int
dres_fallback_call(void *data, char *name,
                   vm_stack_entry_t *args, int narg, vm_stack_entry_t *rv)
{
    dres_t *dres = (dres_t *)data;

    if (dres->fallback) 
        return dres->fallback(data, name, args, narg, rv);
    else {
        DEBUG(DBG_RESOLVE, "unknown action %s", name);
        /* XXX TODO: dump arguments */
        return EINVAL;
    }
    
}


/********************
 * dres_fallback_handler
 ********************/
int
dres_fallback_handler(dres_t *dres, dres_handler_t handler)
{
    dres->fallback = handler;
    return 0;
}


/********************
 * dres_register_builtins
 ********************/
int
dres_register_builtins(dres_t *dres)
{
    dres_builtin_t *b;
    int             status;

    for (b = builtins; b->name; b++)
        if ((status = dres_register_handler(dres, b->name, b->handler)) != 0)
            return status;
    
    vm_method_default(&dres->vm, dres_fallback_call);
    
    return 0;
}


#if 0
/********************
 * dres_builtin_assign
 ********************/
BUILTIN_HANDLER(assign)
{
    OhmFactStore *store = ohm_fact_store_get_fact_store();
    GSList       *list;
    
    char     *prefix;
    char      rval[64], factname[64];
    OhmFact **facts;
    int       nfact, i;
    
    if (DRES_ID_TYPE(action->lvalue.variable) != DRES_TYPE_FACTVAR || !ret)
        return EINVAL;
    
    if (action->immediate != DRES_ID_NONE) {
        *ret = NULL;
        return 0;              /* handled in action.c:assign_result */
    }
    
    prefix = dres_get_prefix(dres);
    dres_name(dres, action->rvalue.variable, rval, sizeof(rval));
    snprintf(factname, sizeof(factname), "%s%s", prefix, rval + 1);
    
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
#endif


/********************
 * dres_builtin_dres
 ********************/
BUILTIN_HANDLER(dres)
{
    dres_t       *dres = (dres_t *)data;
    char         *goal;
    unsigned int *pc;
    int           ninstr;
    int           nsize;
    int           status;

    if (narg < 1)                 /* XXX TODO should default to first... */
        return EINVAL;
    
    if (args[0].type != DRES_TYPE_STRING)
        return EINVAL;

    goal = args[0].v.s;
    DEBUG(DBG_RESOLVE, "DRES recursing for goal %s", goal);
    
    pc     = dres->vm.pc;
    ninstr = dres->vm.ninstr;
    nsize  = dres->vm.nsize;

    depth++;
    status = dres_update_goal(dres, goal, NULL);
    depth--;

    dres->vm.pc     = pc;
    dres->vm.ninstr = ninstr;
    dres->vm.nsize  = nsize;

    rv->type = DRES_TYPE_INTEGER;
    rv->v.i  = status;

    return status;
    
    (void)name;
    
#if 0
    dres_call_t *call = action->call;
    char         goal[64];
    int          status;
    
    if (action->call->args == NULL)
        return EINVAL;
    
    goal[0] = '\0';
    dres_print_value(dres, &call->args->value, goal, sizeof(goal));
    
    DEBUG(DBG_RESOLVE, "DRES recursing for goal %s", goal);
    depth++;
    dres_scope_push(dres, call->locals);
    status = dres_update_goal(dres, goal, NULL);
    dres_scope_pop(dres);
    depth--;
    DEBUG(DBG_RESOLVE, "DRES back from goal %s", goal);

    *ret = NULL;
    return status;
#else
    printf("*** %s not implemented\n", __FUNCTION__);
    return 0;
#endif
}


/********************
 * dres_builtin_resolve
 ********************/
BUILTIN_HANDLER(resolve)
{
    return dres_builtin_dres(data, name, args, narg, rv);
}


/********************
 * dres_builtin_echo
 ********************/
BUILTIN_HANDLER(echo)
{
    dres_t       *dres = (dres_t *)data;
    char         *t;
    int           i;
    
    for (i = 0, t = ""; i < narg; i++, t = " ") {
        printf(t);
        switch (args[i].type) {
        case DRES_TYPE_NIL:     printf("<nil>");           break;
        case DRES_TYPE_INTEGER: printf("%d", args[i].v.i); break;
        case DRES_TYPE_DOUBLE:  printf("%f", args[i].v.d); break;
        case DRES_TYPE_STRING:  printf("%s", args[i].v.s); break;
        case DRES_TYPE_FACTVAR:
            vm_global_print(args[i].v.g);
            break;
        default:
            printf("<unknown>");
        }
    }

    printf("\n");
    
    rv->type = DRES_TYPE_INTEGER;
    rv->v.i  = 0;
    return 0;

    (void)dres;
    (void)name;
}


/********************
 * dres_builtin_shell
 ********************/
BUILTIN_HANDLER(shell)
{
    return dres_fallback_call(data, name, args, narg, rv);
}


/********************
 * dres_builtin_fail
 ********************/
BUILTIN_HANDLER(fail)
{
    rv->type = DRES_TYPE_UNKNOWN;
    return EINVAL;

    (void)data;
    (void)name;
    (void)args;
    (void)narg;
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
