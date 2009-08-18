#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <ohm/ohm-fact.h>
#include <dres/dres.h>
#include <dres/compiler.h>
#include "dres-debug.h"


#define BUILTIN_HANDLER(b)                                              \
    static int dres_builtin_##b(void *data, char *name,                 \
                                vm_stack_entry_t *args, int narg,       \
                                vm_stack_entry_t *rv)

BUILTIN_HANDLER(dres);
BUILTIN_HANDLER(resolve);
BUILTIN_HANDLER(echo);
#if 0
BUILTIN_HANDLER(info);
#endif
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
#if 0
    BUILTIN(info),
#endif
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
        DRES_ACTION_ERROR(EINVAL);
    }
    
}


/********************
 * dres_fallback_handler
 ********************/
EXPORTED dres_handler_t
dres_fallback_handler(dres_t *dres, dres_handler_t handler)
{
    dres_handler_t old;

    old            = dres->fallback;
    dres->fallback = handler;

    return old;
}


/********************
 * dres_register_builtins
 ********************/
int
dres_register_builtins(dres_t *dres)
{
    dres_builtin_t *b;
    int             status;
    void           *data;

    for (b = builtins; b->name; b++)
        if ((status = dres_register_handler(dres, b->name, b->handler)) != 0)
            return status;
    
    data = dres;
    vm_method_default(&dres->vm, dres_fallback_call, &data);
    
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

    (void)name;

    if (narg < 1)
        goal = NULL;
    else {
        if (args[0].type != DRES_TYPE_STRING)
            DRES_ACTION_ERROR(EINVAL);
        goal = args[0].v.s;
    }
    
    /* save VM context */
    chunk  = dres->vm.chunk;
    pc     = dres->vm.pc;
    ninstr = dres->vm.ninstr;
    nsize  = dres->vm.nsize;
    info   = dres->vm.info;

    DEBUG(DBG_RESOLVE, "recursively resolving %sgoal %s",
          goal ? "" : "the default ", goal ? goal : "");
    
    status = dres_update_goal(dres, goal, NULL);

    DEBUG(DBG_RESOLVE, "resolved %sgoal %s with status %d (%s)",
          goal ? "" : "the default ", goal ? goal : "", status,
          status < 0 ? "error" : (status ? "success" : "failure"));
    
    /* restore VM context */
    dres->vm.chunk  = chunk;
    dres->vm.pc     = pc;
    dres->vm.ninstr = ninstr;
    dres->vm.nsize  = nsize;
    dres->vm.info   = info;

    rv->type = DRES_TYPE_INTEGER;
    rv->v.i  = status;

    return status;
}


/********************
 * dres_builtin_resolve
 ********************/
BUILTIN_HANDLER(resolve)
{
    return dres_builtin_dres(data, name, args, narg, rv);
}


static FILE *
redirect(char *path, FILE *current)
{
    const char *mode;
    FILE       *fp;

    if (path[0] == '>' && path[1] == '>') {
        path += 2;
        mode  = "a";
    }
    else {
        path++;
        mode = "w";
    }

    if (!strcmp(path, "stdout"))
        fp = stdout;
    else if (!strcmp(path, "stderr"))
        fp = stderr;
    else
        fp = fopen(path, mode);
    
    if (fp == NULL)
        fp = current;
    else
        if (current != stdout && current != stderr)
            fclose(current);
    
    return fp;
}


/********************
 * dres_builtin_echo
 ********************/
BUILTIN_HANDLER(echo)
{
    dres_t *dres = (dres_t *)data;
    FILE   *fp;
    char   *t;
    int     i;

    (void)dres;
    (void)name;
    
    fp = stdout;

    t = "";
    for (i = 0; i < narg; i++) {
        switch (args[i].type) {
        case DRES_TYPE_STRING:
            if (args[i].v.s[0] == '>') {
                fp = redirect(args[i].v.s, fp);
                t = "";
                continue;
            }
            else
                fprintf(fp, "%s%s", t, args[i].v.s);
            break;
            
        case DRES_TYPE_NIL:     fprintf(fp, "%s<nil>", t);           break;
        case DRES_TYPE_INTEGER: fprintf(fp, "%s%d", t, args[i].v.i); break;
        case DRES_TYPE_DOUBLE:  fprintf(fp, "%s%f", t, args[i].v.d); break;
        case DRES_TYPE_FACTVAR:
            fprintf(fp, "%s", t);
            vm_global_print(fp, args[i].v.g);
            break;
        default:
            fprintf(fp, "<unknown>");
        }
        t = " ";
    }
    
    fprintf(fp, "\n");
    fflush(fp);
    
    if (fp != stdout && fp != stderr)
        fclose(fp);

    rv->type = DRES_TYPE_INTEGER;
    rv->v.i  = 0;
    DRES_ACTION_SUCCEED;
}


#if 0
/********************
 * dres_builtin_info
 ********************/
BUILTIN_HANDLER(info)
{
    dres_t *dres = (dres_t *)data;
    FILE   *fp;
    char   *t;
    int     i;

    (void)dres;
    (void)name;
    
    t = "";
    for (i = 0; i < narg; i++) {
        switch (args[i].type) {
        case DRES_TYPE_STRING:
            dres_log(DRES_LOG_INFO, "%s%s", t, args[i].v.s);
            break;
        case DRES_TYPE_NIL:
            dres_log(DRES_LOG_INFO, "%s<nil>", t);
            break;
        case DRES_TYPE_INTEGER:
            dres_log(DRES_LOG_INFO, "%s%d", t, args[i].v.i);
            break;
        case DRES_TYPE_DOUBLE:
            dres_log(DRES_LOG_INFO, "%s%f", t, args[i].v.d);
            break;
        case DRES_TYPE_FACTVAR:
            dres_log(DRES_LOG_INFO, "** TODO: info printing of facts **");
#if 0
            dres_log(DRES_LOG_INFO, "%s", t);
            vm_global_print(fp, args[i].v.g);
#endif
            break;
        default:
            dres_log(DRES_LOG_INFO, "<unknown>");
        }
        t = " ";
    }
    
    rv->type = DRES_TYPE_INTEGER;
    rv->v.i  = 0;
    DRES_ACTION_SUCCEED;
}
#endif


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

    (void)dres;
    (void)name;

    if (narg < 1) {
        DRES_ERROR("builtin 'fact': called with no arguments");
        DRES_ACTION_ERROR(EINVAL);
    }
    
    if ((g = vm_global_alloc(narg)) == NULL) {
        DRES_ERROR("builtin 'fact': failed to allocate new global");
        DRES_ACTION_ERROR(ENOMEM);
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
    DRES_ACTION_SUCCEED;

 fail:
    if (g)
        vm_global_free(g);
    
    DRES_ACTION_ERROR(err);    
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

    (void)data;
    (void)name;
    
    if (narg > 0 && args[0].type == DRES_TYPE_INTEGER)
        err = args[0].v.i;
    else
        err = EINVAL;
    
    rv->type = DRES_TYPE_UNKNOWN;
    DRES_ACTION_ERROR(err);
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
