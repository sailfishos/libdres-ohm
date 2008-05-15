#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <dres/dres.h>


/*****************************************************************************
 *                        *** target action handling ***                     *
 *****************************************************************************/

/********************
 * dres_new_action
 ********************/
dres_action_t *
dres_new_action(int argument)
{
    dres_action_t *action;
    
    if (ALLOC_OBJ(action) == NULL)
        return NULL;
    
    action->lvalue.variable = DRES_ID_NONE;
    action->rvalue.variable = DRES_ID_NONE;

    if (argument != DRES_ID_NONE && dres_add_argument(action, argument)) {
        dres_free_actions(action);
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
    if (!REALLOC_ARR(action->arguments, action->nargument, action->nargument+1))
        return ENOMEM;

    action->arguments[action->nargument++] = argument;
    
    return 0;
}


/********************
 * dres_add_assignment
 ********************/
int
dres_add_assignment(dres_action_t *action, int var, int val)
{
    if (!REALLOC_ARR(action->variables, action->nvariable, action->nvariable+1))
        return ENOMEM;
    
    action->variables[action->nvariable].var_id = var;
    action->variables[action->nvariable].val_id = val;
    action->nvariable++;
    
    return 0;

}


/********************
 * dres_free_actions
 ********************/
void
dres_free_actions(dres_action_t *actions)
{
    dres_action_t *a, *p;
    
    if (actions == NULL)
        return;
    
    for (a = actions, p = NULL; a->next != NULL; p = a, a = a->next) {
        FREE(p);
        FREE(a->name);
        FREE(a->arguments);
    }
    
    FREE(p);
}



/*****************************************************************************
 *                           *** action handlers ***                         *
 *****************************************************************************/

/********************
 * dres_register_handler
 ********************/
int
dres_register_handler(dres_t *dres, char *name,
                      int (*handler)(dres_t *,
                                     char *, dres_action_t *, void **))
{
    if (!REALLOC_ARR(dres->handlers, dres->nhandler, dres->nhandler + 1))
        return ENOMEM;
    
    if ((name = STRDUP(name)) == NULL)
        return ENOMEM;
    
    dres->handlers[dres->nhandler].name    = name;
    dres->handlers[dres->nhandler].handler = handler;
    dres->nhandler++;
    
    return 0;
}


/********************
 * dres_lookup_handler
 ********************/
dres_handler_t *
dres_lookup_handler(dres_t *dres, char *name)
{
    dres_handler_t *h;
    int             i;

    for (i = 0, h = dres->handlers; i < dres->nhandler; i++, h++)
        if (!strcmp(h->name, name))
            return h;
    
    return NULL;
}


/********************
 * dres_run_actions
 ********************/
int
dres_run_actions(dres_t *dres, dres_target_t *target)
{
    dres_action_t  *action;
    dres_handler_t *handler;
    void           *retval;
    int             status;

    DEBUG("executing actions for %s", target->name);

    /* XXX TODO retval processing */
    
    status = 0;
    for (action = target->actions; action != NULL; action = action->next) {
        dres_dump_action(dres, action);
        handler = action->handler;
        status |= handler->handler(dres, handler->name, action, &retval);
    }
    
    return status;
}


/********************
 * dres_dump_action
 ********************/
void
dres_dump_action(dres_t *dres, dres_action_t *action)
{
    dres_action_t *a = action;
    dres_assign_t *v;
    int            i, j;
    char           lvalbuf[128], *lval, rvalbuf[128], *rval;
    char           arg[64], var[64], val[64], *t;
    char           actbuf[1024], *p;

    if (action == NULL)
        return;
    
    p = actbuf;

    lval = dres_dump_varref(dres, lvalbuf, sizeof(lvalbuf), &a->lvalue);
    rval = dres_dump_varref(dres, rvalbuf, sizeof(rvalbuf), &a->rvalue);
    if (lval)
        p += sprintf(p, "%s = ", lval);
    if (rval)
        p += sprintf(p, "%s", rval);
    else {
        p += sprintf(p, "  %s(", a->name);
        for (i = 0, t = ""; i < a->nargument; i++, t=",")
            p += sprintf(p, "%s%s", t,
                         dres_name(dres, a->arguments[i], arg, sizeof(arg)));
        for (j = 0, v = a->variables; j < a->nvariable; j++, v++, t=",") {
            p += sprintf(p, "%s%s=%s", t,
                         dres_name(dres, v->var_id, var, sizeof(var)),
                         dres_name(dres, v->val_id, val, sizeof(val)));
        }
        sprintf(p, ")");
    }

    DEBUG("action %s", actbuf);
}






/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
