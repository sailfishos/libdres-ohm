#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <dres/dres.h>


static int dres_builtin_dres(dres_t *dres,
                             char *name, dres_action_t *action, void **ret);
#if 0
static int dres_builtin_prolog(dres_t *dres,
                               char *name, dres_action_t *action, void **ret);
#endif
static int dres_builtin_shell(dres_t *dres,
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
        BUILTIN(dres),
        BUILTIN(shell),
#if 0
        BUILTIN(prolog),
#endif
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
 * dres_builtin_dres
 ********************/
static int
dres_builtin_dres(dres_t *dres, char *name, dres_action_t *action, void **ret)
{
    char  buf[64];
    char *goal;

    /* XXX TODO: factstore forking, local variables with nested scoping */

    if (action->arguments == NULL)
        return EINVAL;

    goal = dres_name(dres, action->arguments[0], buf, sizeof(buf));

    DEBUG("DRES recursing for goal %s", goal);
    depth++;
    dres_update_goal(dres, goal);
    depth--;
    DEBUG("DRES back from goal %s", goal);

    return 0;
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
    dres_assign_t *v;
    int            i, j;
    char           lval[64], arg[64], var[64], val[64], *t;
    char           buf[1024], *p;

    if (action == NULL)
        return 0;

    DEBUG("unknown action %s", name);

    p  = buf;
    p += sprintf(p, "%s%s%s(",
                 action->lvalue != DRES_ID_NONE ?
                 dres_name(dres, action->lvalue, lval, sizeof(lval)): "",
          action->lvalue != DRES_ID_NONE ? " = " : "", action->name);
    for (i = 0, t = ""; i < action->nargument; i++, t=",")
        p += sprintf(p, "%s%s", t,
                     dres_name(dres, action->arguments[i], arg, sizeof(arg)));
    for (j = 0, v = action->variables; j < action->nvariable; j++, v++, t=",") {
        p += sprintf(p, "%s%s=%s", t,
                     dres_name(dres, v->var_id, var, sizeof(var)),
                     dres_name(dres, v->val_id, val, sizeof(val)));
    }
    
    sprintf(p, ")");
    DEBUG("%s", buf);

    return 0;
}





/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
