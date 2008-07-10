#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <ohm/ohm-fact.h>
#include <dres/dres.h>
#include "dres-debug.h"

#define BUILTIN_HANDLER(b) \
    static int dres_builtin_##b(dres_t *dres, \
                                char *name, dres_action_t *action, void **ret)

BUILTIN_HANDLER(assign);
BUILTIN_HANDLER(dres);
BUILTIN_HANDLER(resolve);
BUILTIN_HANDLER(echo);
BUILTIN_HANDLER(shell);
BUILTIN_HANDLER(fail);
BUILTIN_HANDLER(unknown);

#define BUILTIN(b) { .name = #b, .handler = dres_builtin_##b }

static dres_handler_t builtins[] = {
    { .name = DRES_BUILTIN_ASSIGN, .handler = dres_builtin_assign },
    BUILTIN(dres),
    BUILTIN(resolve),
    BUILTIN(echo),
    BUILTIN(shell),
    BUILTIN(fail),
    { .name = DRES_BUILTIN_UNKNOWN, .handler = dres_builtin_unknown },
    { .name = NULL, .handler = NULL }
};


/*****************************************************************************
 *                            *** builtin handlers ***                       *
 *****************************************************************************/

/********************
 * dres_fallback_handler
 ********************/
int
dres_fallback_handler(dres_t *dres,
                      int (*handler)(dres_t *,
                                     char *, dres_action_t *, void **))
{
    dres->fallback.name    = "fallback";
    dres->fallback.handler = handler;
    return 0;
}


/********************
 * dres_register_builtins
 ********************/
int
dres_register_builtins(dres_t *dres)
{
    dres_handler_t *h;
    int             status;

    for (h = builtins; h->name; h++)
        if ((status = dres_register_handler(dres, h->name, h->handler)) != 0)
            return status;

    return 0;
}


/********************
 * dres_builtin_assign
 ********************/
BUILTIN_HANDLER(assign)
{
    OhmFactStore *store = ohm_fact_store_get_fact_store();
    GSList       *list;
    
    char     *prefix;
    char      rval[64], factname[64];
    OhmFact **facts = NULL;
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


/********************
 * dres_builtin_dres
 ********************/
BUILTIN_HANDLER(dres)
{
    char goal[64];
    int  status;
    
    /* XXX TODO: factstore transaction */
    
    if (action->arguments == NULL)
        return EINVAL;
    
    dres_name(dres, action->arguments[0], goal, sizeof(goal));
    
    DEBUG(DBG_RESOLVE, "DRES recursing for goal %s", goal);
    depth++;
    dres_scope_push(dres, action->variables, action->nvariable);
    status = dres_update_goal(dres, goal, NULL);
    dres_scope_pop(dres);
    depth--;
    DEBUG(DBG_RESOLVE, "DRES back from goal %s", goal);

    *ret = NULL;
    return status;
}


/********************
 * dres_builtin_resolve
 ********************/
BUILTIN_HANDLER(resolve)
{
    return dres_builtin_dres(dres, name, action, ret);
}


/********************
 * dres_builtin_echo
 ********************/
BUILTIN_HANDLER(echo)
{
#define MAX_LENGTH 64
#define PRINT(s)              \
    do {                      \
        unsigned int l = strlen(s);    \
        if (l < (unsigned int)((e-p)-1)) {    \
            strcpy(p, s);     \
            p += l;           \
        }                     \
        else if ((unsigned int)(e-p) > 0) {   \
            l = (e-p) - 1;    \
            strncpy(p, s, l); \
            p[l] = '\0';      \
            p += l;           \
        }                     \
    } while(0)

    dres_variable_t *var;
    char             arg[MAX_LENGTH];
    char             buf[4096];
    char            *p, *e, *str;
    unsigned int     i;

    buf[0] = '\0';

    for (i = 0, e = (p = buf) + sizeof(buf); i < action->nargument; i++) {
        
        dres_name(dres, action->arguments[i], arg, MAX_LENGTH);

        switch (arg[0]) {
        case '&':
            if ((str = dres_scope_getvar(dres->scope, arg+1)) == NULL)
                PRINT("???");
            else {
                PRINT(str);
                free(str);
            }
            break;

        case '$':
            if (!(var = dres_lookup_variable(dres, action->arguments[i])) ||
                !dres_var_get_field(var->var, "value", NULL, VAR_STRING, &str))
                PRINT("???");
            else {
                PRINT(str);
                free(str);
            }
            break;

        default:
            PRINT(arg);
            break;
        }

        PRINT(" ");
    }

    printf("%s\n", buf);

    if (ret != NULL)
        *ret = NULL;

    return 0;

#undef PRINT
#undef MAX_LENGTH
}


/********************
 * dres_builtin_shell
 ********************/
BUILTIN_HANDLER(shell)
{
    return dres_builtin_unknown(dres, name, action, ret);
}


/********************
 * dres_builtin_fail
 ********************/
BUILTIN_HANDLER(fail)
{
    *ret = NULL;
    return EINVAL;
}


/********************
 * dres_builtin_unknown
 ********************/
BUILTIN_HANDLER(unknown)
{
    if (dres->fallback.handler != NULL)
        return dres->fallback.handler(dres, name, action, ret);
    else {
        if (action == NULL)
            return 0;
    
        DEBUG(DBG_RESOLVE, "unknown action %s", name);
    
        printf("*** unknown action %s", name);
        dres_dump_action(dres, action);
        
        return EINVAL;
    }
}





/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
