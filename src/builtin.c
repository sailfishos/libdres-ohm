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

BUILTIN_HANDLER(dres);
BUILTIN_HANDLER(resolve);
BUILTIN_HANDLER(echo);
BUILTIN_HANDLER(fact);
BUILTIN_HANDLER(shell);
BUILTIN_HANDLER(fail);

#define BUILTIN(b) { .name = #b, .handler = dres_builtin_##b }

typedef struct dres_builtin_s {
    char           *name;
    dres_handler_t  handler;
} dres_builtin_t;

static dres_builtin_t builtins[] = {
    BUILTIN(dres),
    BUILTIN(resolve),
    BUILTIN(echo),
    BUILTIN(fact),
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


/********************
 * dres_builtin_dres
 ********************/
BUILTIN_HANDLER(dres)
{
    dres_t       *dres = (dres_t *)data;
    char         *goal;
    vm_chunk_t   *chunk;
    unsigned int *pc;
    int           ninstr;
    int           nsize;
    int           status;
    const char   *info;

    if (narg < 1)
        goal = NULL;
    else {
        if (args[0].type != DRES_TYPE_STRING)
            return EINVAL;
        goal = args[0].v.s;
    }
    
    /* save VM context */
    chunk  = dres->vm.chunk;
    pc     = dres->vm.pc;
    ninstr = dres->vm.ninstr;
    nsize  = dres->vm.nsize;
    info   = dres->vm.info;

    DEBUG(DBG_RESOLVE, "DRES recursing for %sgoal %s",
          goal ? "" : "the default ", goal ? goal : "");
    
    status = dres_update_goal(dres, goal, NULL);

    DEBUG(DBG_RESOLVE, "DRES back from goal %s", goal);

    /* restore VM context */
    dres->vm.chunk  = chunk;
    dres->vm.pc     = pc;
    dres->vm.ninstr = ninstr;
    dres->vm.nsize  = nsize;
    dres->vm.info   = info;

    rv->type = DRES_TYPE_INTEGER;
    rv->v.i  = status;

    return status;
    
    (void)name;
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
 * dres_builtin_fact
 ********************/
BUILTIN_HANDLER(fact)
{
    dres_t       *dres = (dres_t *)data;
    vm_global_t  *g;
    GValue       *value;
    char         *field;
    int           a, i, err;

    if (narg < 1) {
        DRES_ERROR("builtin 'fact': called with no arguments");
        return EINVAL;
    }
    
    if ((g = vm_global_alloc(narg)) == NULL) {
        DRES_ERROR("builtin 'fact': failed to allocate new global");
        return ENOMEM;
    }
    
    for (a = 0, i = 0; a < narg && i < narg; a++) {
        g->facts[a] = ohm_fact_new("foo");
        while (i < narg) {
            if (args[i].type != DRES_TYPE_STRING) {
                DRES_ERROR("builtin 'fact': invalid field name (type 0x%x)",
                           args[i].type);
                err = EINVAL;
                goto fail;
            }

            field = args[i].v.s;
            if (!field[0]) {
                i++;
                break;
            }
            
            if (i == narg - 1) {
                DRES_ERROR("builtin 'fact': missing value for field %s", field);
                err = EINVAL;
                goto fail;
            }

            i++;
            
            switch (args[i].type) {
            case DRES_TYPE_INTEGER:
                value = ohm_value_from_int(args[i].v.i);
                break;
            case DRES_TYPE_STRING:
                value = ohm_value_from_string(args[i].v.s);
                break;
            case DRES_TYPE_DOUBLE:
                value = ohm_value_from_double(args[i].v.d);
                break;
            default:
                DRES_ERROR("builtin 'fact': invalid value for field %s", field);
                err = EINVAL;
                goto fail;
            }
            
            ohm_fact_set(g->facts[a], field, value);
            
            i++;
        }
    }
    
    g->nfact = a;

    rv->type = DRES_TYPE_FACTVAR;
    rv->v.g  = g;
    return 0;

 fail:
    if (g)
        vm_global_free(g);
    
    return err;
    
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
    int err;
    
    if (narg > 0 && args[0].type == DRES_TYPE_INTEGER)
        err = args[0].v.i;
    else
        err = EINVAL;
    
    rv->type = DRES_TYPE_UNKNOWN;
    return err;

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
