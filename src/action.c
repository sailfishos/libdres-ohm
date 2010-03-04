#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <glib-object.h>

#include <dres/dres.h>
#include <dres/compiler.h>
#include "dres-debug.h"

/*****************************************************************************
 *                             *** method calls ***                          *
 *****************************************************************************/


/*****************************************************************************
 *                        *** target action handling ***                     *
 *****************************************************************************/

/********************
 * dres_free_locals
 ********************/
void
dres_free_locals(dres_local_t *locals)
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
 * dres_free_varref
 ********************/
void
dres_free_varref(dres_varref_t *vref)
{
    dres_select_t *p, *n;

    if (vref->variable == DRES_ID_NONE)
        return;
    
    for (p = vref->selector; p != NULL; p = n) {
        n = p->next;
        dres_free_field(&p->field);
        FREE(p);
    }
    FREE(vref->field);
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
    int status;
    
    if (!DRES_TST_FLAG(dres, COMPILED))
        return vm_method_add(&dres->vm, name, handler, dres);
    else {
        status = vm_method_set(&dres->vm, name, handler, dres);
        /*
         * Notes:
         *   For compiled rulesets, registering a handler for a non-
         *   existing method is not treated as an error. As the VM is
         *   never going to attempt to call the method (that is why it
         *   is non-existent) no harm is done. If we start compiling
         *   extra code for precompiled rulesets (eg. for the debug
         *   console) this might need different treatment (eg. the
         *   introduction of a dynamic method table).
         */
        return (status == 0 || status == ENOENT ? 0 : status);
    }
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

    char *p, *t, value[128], *op;
    int   left, n;

    p    = buf;
    left = size - 1;
    *p   = '\0';

    for (t = ""; s != NULL; s = s->next, t = ", ") {
        op = s->op == DRES_OP_NEQ ? "!" : "";
        P("%s%s", t, s->field.name);
        if (s->field.value.type != DRES_TYPE_UNKNOWN) {
            dres_print_value(dres, &s->field.value, value, sizeof(value));
            P(":%s%s", op, value);
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
        
    if (left > 0)
        *p = '\0';

    return size - left - 1;
#undef P    
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
