#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <glib-object.h>

#include <dres/dres.h>
#include <dres/compiler.h>
#include "dres-debug.h"

static void free_args  (dres_arg_t *args);
static void free_locals(dres_local_t *locals);



/*****************************************************************************
 *                             *** method calls ***                          *
 *****************************************************************************/

/********************
 * dres_new_call
 ********************/
dres_call_t *
dres_new_call(dres_t *dres, char *name, dres_arg_t *args, dres_local_t *locals)
{
    dres_call_t *call;
    int          err;
    
    if ((err = dres_register_handler(dres, name, NULL)) != 0 && err != EEXIST)
        return NULL;
    
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
    if (DRES_TST_FLAG(dres, COMPILED))
        return EINVAL;
    else
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
    int status;

    DEBUG(DBG_RESOLVE, "executing actions for %s", target->name);

    if (target->code == NULL)
        status = TRUE;
    else
        status = vm_exec(&dres->vm, target->code);
    
    return status;
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

    char *p, *t, value[128];
    int   left, n;

    p    = buf;
    left = size - 1;
    *p   = '\0';

    for (t = ""; s != NULL; s = s->next, t = ", ") {
        P("%s%s", t, s->field.name);
        if (s->field.value.type != DRES_TYPE_UNKNOWN) {
            dres_print_value(dres, &s->field.value, value, sizeof(value));
            P(":%s", value);
        }
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
    char buf[1024];

    dres_print_action(dres, action, buf, sizeof(buf) - 1);
    buf[sizeof(buf)-1] = '\0';
    
    printf("%s", buf);
}



/********************
 * dres_print_action
 ********************/
int
dres_print_action(dres_t *dres, dres_action_t *action, char *buf, size_t size)
{
#define P(fmt, args...) do {                        \
        n     = snprintf(p, left, fmt, ## args);    \
        p    += n;                                  \
        left -= n;                                  \
    } while (0)
    
    dres_action_t *a = action;
    char          *p;
    int            left, n;

    if (a == NULL)
        return 0;

    p    = buf;
    left = size - 1;
    
    if (a->lvalue.variable != DRES_ID_NONE) {
        n = dres_print_varref(dres, &a->lvalue, p, left);
        p    += n;
        left -= n;

        n     = snprintf(p, left, " %s ", a->op == DRES_ASSIGN_PARTIAL ?
                         "|=" : "=");
        p    += n;
        left -= n;
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

    return size - left - 1;
}




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
