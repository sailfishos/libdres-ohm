#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <glib-object.h>

#include <dres/dres.h>
#include <dres/compiler.h>
#include "dres-debug.h"

static int assign_result(dres_t *dres, dres_action_t *action, void **facts);


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
    action->immediate       = DRES_ID_NONE;

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
#if 1
int
dres_add_assignment(dres_action_t *action, dres_assign_t *assignment)
{
    if (!REALLOC_ARR(action->variables, action->nvariable, action->nvariable+1))
        return ENOMEM;
    
    action->variables[action->nvariable] = *assignment;
    action->nvariable++;
    
    return 0;
}
#else
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
#endif


/********************
 * dres_dump_assignment
 ********************/
char *
dres_dump_assignment(dres_t *dres, dres_assign_t *a, char *buf, size_t size)
{
    char *p;
    int   len;

    if (dres_dump_varref(dres, buf, size, &a->lvalue) == NULL)
        return NULL;
    len   = strlen(buf);
    p     = buf + len;
    size -= len;
    
    if (size < 4)
        return NULL;
    
    p[0] = ' ';
    p[1] = '=';
    p[2] = ' ';
    p    += 3;
    size += 3;
    
    switch (a->type) {
    case DRES_ASSIGN_IMMEDIATE:
        dres_name(dres, a->val, p, size);
        break;

    case DRES_ASSIGN_VARIABLE:
        if (dres_dump_varref(dres, p, size, &a->var) == NULL)
            return NULL;
        break;
    }

    return buf;
}



/********************
 * free_args
 ********************/
static void
free_args(dres_arg_t *args)
{
    dres_arg_t *a, *n;

    for (a = args; a != NULL; a = n) {
        n = a->next;
        if (a->value.type == DRES_TYPE_STRING)
            FREE(a->value.v.s);
        FREE(a);
    }
}


/********************
 * free_locals
 ********************/
static void
free_locals(dres_local_t *locals)
{
    dres_local_t *l, *n;

    for (l = locals; l != NULL; l = n) {
        n = l->next;
        if (l->value.type == DRES_TYPE_STRING)
            FREE(l->value.v.s);
        FREE(l);
    }
}


/********************
 * dres_free_actions
 ********************/
void
dres_free_actions(dres_action_t *actions)
{
    dres_action_t *a, *n;

    if (actions == NULL)
        return;
    
    for (a = actions; a != NULL; a = n) {
        n = a->next;
        FREE(a->name);
        free_args(a->args);
        free_locals(a->locals);
        FREE(a->arguments);
        /* XXX hmm... how about a->variables in the old code/branch ? */
    }
}



/*****************************************************************************
 *                           *** action handlers ***                         *
 *****************************************************************************/

/********************
 * dres_register_handler
 ********************/
EXPORTED int
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
EXPORTED dres_handler_t *
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
    int             err;

    DEBUG(DBG_RESOLVE, "executing actions for %s", target->name);

    err = 0;
#if NESTED_TRANSACTIONS_DONT_WORK
    dres_store_tx_new(dres->fact_store);
#endif

    for (action = target->actions; !err && action; action = action->next) {
        handler = action->handler;
        if ((err = handler->handler(dres, handler->name, action, &retval)))
            continue;
    
        err = assign_result(dres, action, retval);
    }

#if NESTED_TRANSACTIONS_DONT_WORK
    if (err)
        dres_store_tx_rollback(dres->fact_store);
    else
        dres_store_tx_commit(dres->fact_store);
#endif
    
    return err;
}


/********************
 * dres_dump_args
 ********************/
void
dres_dump_args(dres_t *dres, dres_arg_t *args)
{
    dres_arg_t *a;
    char       *t, name[128];

    for (a = args, t = ""; a != NULL; a = a->next, t = ", ") {
        switch (a->value.type) {
        case DRES_TYPE_INTEGER: printf("%s%d", t, a->value.v.i); break;
        case DRES_TYPE_DOUBLE:  printf("%s%f", t, a->value.v.d); break;
        case DRES_TYPE_STRING:  printf("%s%s", t, a->value.v.s); break;
        case DRES_TYPE_FACTVAR:
        case DRES_TYPE_DRESVAR:
            dres_name(dres, a->value.v.id, name, sizeof(name));
            printf("%s%s", t, name);
            break;
        default:
            printf("%s<unknown>", t);
        }
    }
}


/********************
 * dres_print_args
 ********************/
int
dres_print_args(dres_t *dres, dres_arg_t *args, char *buf, size_t size)
{
#define P(fmt, args...) do {                        \
        n     = snprintf(p, left, fmt, ## args);    \
        p    += n;                                  \
        left -= n;                                  \
    } while (0)
    

    dres_arg_t *a;
    char       *p, *t, name[128];
    int         left, n;
    
    p    = buf;
    left = size;
    for (a = args, n = 0, t = ""; a != NULL; a = a->next, t = ", ") {
        switch (a->value.type) {
        case DRES_TYPE_INTEGER: P("%s%d", t, a->value.v.i); break;
        case DRES_TYPE_DOUBLE:  P("%s%f", t, a->value.v.d); break;
        case DRES_TYPE_STRING:  P("%s%s", t, a->value.v.s); break;
        case DRES_TYPE_FACTVAR:
        case DRES_TYPE_DRESVAR:
            dres_name(dres, a->value.v.id, name, sizeof(name));
            P("%s%s", t, name);
            break;
        default:
            P("%s<unknown>", t);
        }
    }

    return size - left;
#undef P
}


/********************
 * dres_print_locals
 ********************/
int
dres_print_locals(dres_t *dres, dres_local_t *locals, char *buf, size_t size)
{
#define P(fmt, args...) do {                        \
        n     = snprintf(p, left, fmt, ## args);  \
        p    += n;                                  \
        left -= n;                                  \
    } while (0)
    
    dres_local_t *l;
    char         *p, *t, name[128];
    int           left, n;
    

    p    = buf;
    left = size;
    for (l = locals, n = 0, t = ""; l != NULL; l = l->next, t = ", ") {
        dres_name(dres, l->id, name, sizeof(name));
        P("%s%s = ", t, name);
        switch (l->value.type) {
        case DRES_TYPE_INTEGER: P("%d", l->value.v.i); break;
        case DRES_TYPE_DOUBLE:  P("%f", l->value.v.d); break;
        case DRES_TYPE_STRING:  P("%s", l->value.v.s); break;
        case DRES_TYPE_DRESVAR:
            dres_name(dres, l->value.v.id, name, sizeof(name));
            P("%s", name);
            break;
        default:
            P("%s<unknown>", t);
        }
    }

    return size - left;
}


/********************
 * dres_dump_action
 ********************/
void
dres_dump_action(dres_t *dres, dres_action_t *action)
{
    dres_action_t *a = action;
#if 0
    int            i, j;
    char          *t, arg[64], buf[128];
    dres_assign_t *v;
#endif

    char           lvalbuf[128], *lval, rvalbuf[128], *rval, val[64];
    char           actbuf[1024], *p;

    if (action == NULL)
        return;
    

    /*
     * XXX TODO rewrite with s/sprintf/snprintf/g ...
     */

    p = actbuf;

    lval = dres_dump_varref(dres, lvalbuf, sizeof(lvalbuf), &a->lvalue);
    rval = dres_dump_varref(dres, rvalbuf, sizeof(rvalbuf), &a->rvalue);
    if (lval)
        p += sprintf(p, "%s = ", lval);

    if (action->immediate != DRES_ID_NONE) {
        dres_name(dres, action->immediate, val, sizeof(val));
        p += sprintf(p, "%s", val);
    }
    else if (rval)
        p += sprintf(p, "%s", rval);
    else {
        p += sprintf(p, "%s(", a->name);
#if 1
        p += dres_print_args(dres, a->args, p, 1024);
        if (a->locals)
            p += sprintf(p, ", ");
        p += dres_print_locals(dres, a->locals, p, 1024);
#else
        for (i = 0, t = ""; i < a->nargument; i++, t=",")
            p += sprintf(p, "%s%s", t,
                         dres_name(dres, a->arguments[i], arg,sizeof(arg)));
        for (j = 0, v = a->variables; j < a->nvariable; j++, v++, t=",") {
            if (dres_dump_assignment(dres, v, buf, sizeof(buf)) == NULL)
                sprintf(buf, "<invalid assignment>");
            
            p += sprintf(p, "%s%s", t, buf);
        }
#endif
        sprintf(p, ")");
    }

    printf("    %s\n", actbuf);
}


/********************
 * assing_result
 ********************/
static int
assign_result(dres_t *dres, dres_action_t *action, void **result)
{
#define MAX_FACTS 64
#define FAIL(ec) do { err = ec; goto out; } while(0)

    struct {
        dres_array_t  head;
        void         *facts[MAX_FACTS];
    } arrbuf = { head: { len: 0 } };
    dres_array_t     *facts = &arrbuf.head;
    
    dres_variable_t *var;
    dres_vartype_t   type;
    char             name[128], factname[128], *prefix, value[128];
    int              nfact, i, err;
    

    
    /*
     * XXX TODO
     *   This whole fact mangling/copying/converting to dres_array_t is so
     *   butt-ugly that it needs to be cleaned up. The least worst might be
     *   to put a dres_array_t to dres_action_t and use that everywhere.
     */
    
    for (nfact = 0; result && result[nfact] != NULL; nfact++)
        ;
    
    err = 0;
    switch (DRES_ID_TYPE(action->lvalue.variable)) {

    case DRES_TYPE_FACTVAR:
        prefix = dres_get_prefix(dres);
        dres_name(dres, action->lvalue.variable, name, sizeof(name));
        snprintf(factname, sizeof(factname), "%s%s", prefix, name + 1);
        
#if 0
        if (action->lvalue.field != NULL) {
            DEBUG(DBG_RESOLVE, "uh-oh... should set lvalue.field...");
            FAIL(EINVAL);
        }
#endif
        
        if (!(var = dres_lookup_variable(dres, action->lvalue.variable)))
            FAIL(ENOENT);
      
        if (action->immediate != DRES_ID_NONE) {
            dres_name(dres, action->immediate, value, sizeof(value));
            if (!dres_var_set_field(var->var, action->lvalue.field,
                                    action->lvalue.selector,
                                    VAR_STRING, &value))
                FAIL(EINVAL);
            return 0;
        }

        facts->len = nfact;
        memcpy(&facts->fact[0], result, nfact * sizeof(result[0]));
        type = VAR_FACT_ARRAY;
        if (!dres_var_set(var->var, action->lvalue.selector, type, facts))
            FAIL(EINVAL);

        break;
    }

 out:
    for (i = 0; i < nfact; i++)
        g_object_unref(result[i]);
    FREE(result);

    return err;
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
