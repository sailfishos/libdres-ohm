#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <glib-object.h>

#include <dres/dres.h>
#include <dres/compiler.h>
#include "dres-debug.h"

static int  assign_result(dres_t *dres, dres_action_t *action, void **facts);
static int  assign_value (dres_t *dres, dres_action_t *action,
                          dres_variable_t *var);

static void free_args  (dres_arg_t *args);
static void free_locals(dres_local_t *locals);



/*****************************************************************************
 *                             *** method calls ***                          *
 *****************************************************************************/

/********************
 * dres_new_call
 ********************/
dres_call_t *
dres_new_call(char *name, dres_arg_t *args, dres_local_t *locals)
{
    dres_call_t *call;

    if (ALLOC_OBJ(call) == NULL)
        return NULL;

    if ((call->name = STRDUP(name)) == NULL) {
        FREE(call);
        return NULL;
    }
    
    call->args   = args;
    call->locals = locals;

    return call;
}


/********************
 * dres_free_call
 ********************/
void
dres_free_call(dres_call_t *call)
{
    if (call) {
        FREE(call->name);
        free_args(call->args);
        free_locals(call->locals);
        FREE(call);
    }
}


/*****************************************************************************
 *                        *** target action handling ***                     *
 *****************************************************************************/

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

        if (a->type == DRES_ACTION_CALL)
            dres_free_call(a->call);
        
        FREE(a);
    }
}



/*****************************************************************************
 *                           *** action handlers ***                         *
 *****************************************************************************/

/********************
 * dres_register_handler
 ********************/
EXPORTED int
dres_register_handler(dres_t *dres, char *name, dres_handler_t handler)
{
    return vm_method_add(&dres->vm, name, handler, dres);
}


/********************
 * dres_lookup_handler
 ********************/
EXPORTED dres_handler_t
dres_lookup_handler(dres_t *dres, char *name)
{
    vm_method_t *m = vm_method_lookup(&dres->vm, name);

    return m ? m->handler : NULL;
}


/********************
 * dres_run_actions
 ********************/
int
dres_run_actions(dres_t *dres, dres_target_t *target)
{
    int err;

    DEBUG(DBG_RESOLVE, "executing actions for %s", target->name);

    if (target->code == NULL)
        return 0;
    
    err = vm_exec(&dres->vm, target->code);
    
    return err;
    
#if 0

    dres_action_t  *action;
    dres_handler_t *handler;
    void           *retval;
    int             err;


#if NESTED_TRANSACTIONS_DO_WORK
    dres_store_tx_new(dres->fact_store);
#endif

    for (action = target->actions; !err && action; action = action->next) {
        if (action->type != DRES_ACTION_CALL) {
            printf("***** skipping non-call action...\n");
            printf("***** what about $a = $b type assignments ?\n");
            continue;
        }
        
        handler = action->call->handler;
        if ((err = handler->handler(dres, handler->name, action, &retval)))
            continue;
    
        err = assign_result(dres, action, retval);
    }

#if NESTED_TRANSACTIONS_DO_WORK
    if (err)
        dres_store_tx_rollback(dres->fact_store);
    else
        dres_store_tx_commit(dres->fact_store);
#endif
#endif    


    return err;
}


/********************
 * dres_copy_value
 ********************/
dres_value_t *
dres_copy_value(dres_value_t *value)
{
    dres_value_t *copy;

    if (ALLOC_OBJ(copy) == NULL)
        return NULL;

    *copy = *value;
    if (copy->type == DRES_TYPE_STRING)
        if ((copy->v.s = STRDUP(copy->v.s)) == NULL) {
            FREE(copy);
            return NULL;
        }

    return copy;
}


/********************
 * dres_free_value
 ********************/
void
dres_free_value(dres_value_t *value)
{
    if (value) {
        if (value->type == DRES_TYPE_STRING)
            FREE(value->v.s);
        FREE(value);
    }
}


/********************
 * dres_print_value
 ********************/
int
dres_print_value(dres_t *dres, dres_value_t *value, char *buf, size_t size)
{
    char *p, name[128];
    int   n;

    p = buf;
    n = size;
    switch (value->type) {
    case DRES_TYPE_INTEGER: n = snprintf(p, size, "%d", value->v.i); break;
    case DRES_TYPE_DOUBLE:  n = snprintf(p, size, "%f", value->v.d); break;
    case DRES_TYPE_STRING:  n = snprintf(p, size, "%s", value->v.s); break;
    case DRES_TYPE_FACTVAR:
    case DRES_TYPE_DRESVAR:
        dres_name(dres, value->v.id, name, sizeof(name));
        n = snprintf(p, size, "%s", name);
        break;
    default:
        n = snprintf(p, size, "<unknown>");
    }
    
    return size - n;
}


/********************
 * dres_dump_args
 ********************/
void
dres_dump_args(dres_t *dres, dres_arg_t *args)
{
    dres_arg_t *a;
    char       *t, value[128];

    for (a = args, t = ""; a != NULL; a = a->next, t = ", ") {
        dres_print_value(dres, &a->value, value, sizeof(value));
        printf("%s%s", t, value);
    }
}


/********************
 * dres_print_args
 ********************/
int
dres_print_args(dres_t *dres, dres_arg_t *args, char *buf, size_t size)
{
    dres_arg_t *a;
    char       *p, *t, value[128];
    int         left, n;
    
    p    = buf;
    left = size - 1;
    for (a = args, n = 0, t = ""; a != NULL; a = a->next, t = ", ") {
        dres_print_value(dres, &a->value, value, sizeof(value));
        n = snprintf(p, left, "%s%s", t, value);
        p    += n;
        left -= n;
    }

    return size - left - 1;
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
    char         *p, *t, tmp[128];
    int           left, n;
    

    p    = buf;
    left = size - 1;
    for (l = locals, n = 0, t = ""; l != NULL; l = l->next, t = ", ") {
        dres_name(dres, l->id, tmp, sizeof(tmp));
        P("%s%s = ", t, tmp);
        dres_print_value(dres, &l->value, tmp, sizeof(tmp));
        P("%s", tmp);
    }

    return size - left - 1;
#undef P
}


/********************
 * dres_print_selector
 ********************/
int
dres_print_selector(dres_t *dres, dres_select_t *s, char *buf, size_t size)
{
#define P(fmt, args...) do {                        \
        n     = snprintf(p, left, fmt, ## args);  \
        p    += n;                                  \
        left -= n;                                  \
    } while (0)

    char *p, value[128];
    int   left, n;

    p    = buf;
    left = size;
    *p   = '\0';

    for (; s != NULL; s = s->next) {
        P("%s:", s->field.name);
        dres_print_value(dres, &s->field.value, value, sizeof(value));
        P("%s", value);
    }

    return size - left - 1;
#undef P
}


/********************
 * dres_print_varref
 ********************/
int
dres_print_varref(dres_t *dres, dres_varref_t *vr, char *buf, size_t size)
{
#define P(fmt, args...) do {                        \
        n     = snprintf(p, left, fmt, ## args);  \
        p    += n;                                  \
        left -= n;                                  \
    } while (0)

    char  *p;
    int   left, n;
    
    if (vr->variable == DRES_ID_NONE)
        return 0;
    
    p    = buf;
    left = size - 1;

    dres_name(dres, vr->variable, buf, left);
    
    n     = strlen(buf);
    p    += n;
    left -= n;

    if (vr->selector != NULL) {
        *p++ = '[';
        left--;
        n  = dres_print_selector(dres, vr->selector, p, left);
        p    += n;
        left -= n;
        if (left > 0) {
            *p++ = ']';
            left--;
        }
    }
    
    if (vr->field != NULL)
        P(":%s", vr->field);
        
    return size - left - 1;
#undef P    
}


/********************
 * dres_print_call
 ********************/
int
dres_print_call(dres_t *dres, dres_call_t *call, char *buf, size_t size)
{
    char *p, *t;
    int   left, n;

    p    = buf;
    left = size - 1;
    t    = "";
    
    n     = snprintf(p, left, "%s(", call->name);
    p    += n;
    left -= n;
    if (call->args) {
        n     = dres_print_args(dres, call->args, p, left);
        p    += n;
        left -= n;
        t     = ", ";
    }
    if (call->locals) {
        n     = snprintf(p, left, ", ");
        p    += n;
        left -= n;
        n     = dres_print_locals(dres, call->locals, p, left);
        p    += n;
        left -= n;
    }
    n    = snprintf(p, left, ")");
    p    += n;
    left -= n;
    
    return size - left - 1;
}


/********************
 * dres_dump_action
 ********************/
void
dres_dump_action(dres_t *dres, dres_action_t *action)
{
#define P(fmt, args...) do {                        \
        n     = snprintf(p, left, fmt, ## args);  \
        p    += n;                                  \
        left -= n;                                  \
    } while (0)
    
    dres_action_t *a = action;
    char           buf[1024], *p;
    int            left, n;

    if (a == NULL)
        return;

    p    = buf;
    left = sizeof(buf) - 1;
    
    if (a->lvalue.variable != DRES_ID_NONE) {
        n = dres_print_varref(dres, &a->lvalue, p, left);
        p    += n;
        left -= n;

        if (left > 3) {
            p[0] = ' ';
            p[1] = '=';
            p[2] = ' ';
            p[3] = '\0';

            p    += 3;
            left -= 3;
        }
    }

    switch (a->type) {
    case DRES_ACTION_VALUE:
        n     = dres_print_value(dres, &a->value, p, left);
        p    += n;
        left -= n;
        break;
    case DRES_ACTION_VARREF:
        n     = dres_print_varref(dres, &a->rvalue, p, left);
        p    += n;
        left -= n;
        break;
    case DRES_ACTION_CALL:
        n     = dres_print_call(dres, a->call, p, left);
        p    += n;
        left -= n;
        break;
    default:
        n     = snprintf(p, left, "<invalid action>");
        p    += n;
        left -= n;
    }

    printf("    %s\n", buf);
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
    char             name[128], factname[128], *prefix;
    int              nfact, i, err;
    
    char          selector[256];

    selector[0] = '\0';
    dres_print_selector(dres, action->lvalue.selector,
                        selector, sizeof(selector));
    
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
        
        if (!(var = dres_lookup_variable(dres, action->lvalue.variable)))
            FAIL(ENOENT);
      
        if (action->type == DRES_ACTION_VALUE) {
            if ((err = assign_value(dres, action, var)))
                FAIL(err);
            else
                return 0;
        }

        facts->len = nfact;
        memcpy(&facts->fact[0], result, nfact * sizeof(result[0]));
        type = VAR_FACT_ARRAY;
        if (!dres_var_set(var->var, selector, type, facts))
            FAIL(EINVAL);
        
        break;
    }

 out:
    for (i = 0; i < nfact; i++)
        g_object_unref(result[i]);
    FREE(result);

    return err;
}



/********************
 * assign_value
 ********************/
static int
assign_value(dres_t *dres, dres_action_t *action, dres_variable_t *var)
{
    dres_value_t *value = &action->value;
    char          selector[256];
    int           status;

    selector[0] = '\0';
    dres_print_selector(dres, action->lvalue.selector,
                        selector, sizeof(selector));

    status = 0;
    switch (value->type) {
    case DRES_TYPE_STRING:
        if (!dres_var_set_field(var->var, action->lvalue.field,
                                selector, VAR_STRING, &value->v.s))
            status = EINVAL;
        break;
    case DRES_TYPE_INTEGER:
        if (!dres_var_set_field(var->var, action->lvalue.field,
                                selector, VAR_INT, &value->v.i))
            status = EINVAL;
        break;
    default:
        status = EOPNOTSUPP;
    }
    
    return status;
}
    


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
