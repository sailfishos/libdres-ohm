#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <glib.h>

#include <dres/dres.h>
#include <dres/compiler.h>

#include "dres-debug.h"

/********************
 * free_name
 ********************/
static void
free_name(gpointer ptr)
{
    char *name = (char *)ptr;

    FREE(name);
}


/********************
 * free_val
 ********************/
static void
free_val(gpointer ptr)
{
    dres_value_t *val = (dres_value_t *)ptr;

    dres_free_value(val);
}



/*
 * Semantics is roughly:
 *
 * dres(goal, &var1=val1, &var2=val2...)
 *
 * will create a new variable store of type STORE_LOCAL and initalize it
 * with var1=val1, var2=val2, ..., and chain it to (ie. push it above) the
 * current local variable store.
 * 
 * Upon returning from the dres call the created local store will be destroyed.
 */


/********************
 * dres_scope_push
 ********************/
EXPORTED int
dres_scope_push(dres_t *dres, dres_local_t *locals)
{
#define FAIL(err) do { status = err; goto fail; } while (0)
    dres_scope_t  *scope;
    dres_local_t  *l;
    dres_value_t  *value;
    char           name[64], varname[64];
    int            status;
    
    
    if (ALLOC_OBJ(scope) == NULL)
        FAIL(ENOMEM);

    if ((scope->curr = dres_store_init(STORE_LOCAL, NULL, NULL)) == NULL)
        FAIL(ENOMEM);

    scope->names = g_hash_table_new_full(g_str_hash, g_str_equal,
                                         free_name, free_val);
    if (scope->names == NULL)
        FAIL(ENOMEM);

    for (l = locals; l != NULL; l = l->next) {
        
        if (DRES_ID_TYPE(l->id) != DRES_TYPE_DRESVAR)
            FAIL(EINVAL);
        
        dres_name(dres, l->id, name, sizeof(name));

        switch (l->value.type) {
        case DRES_TYPE_INTEGER:
        case DRES_TYPE_DOUBLE:
        case DRES_TYPE_STRING:
            if ((status = dres_scope_setvar(scope, name + 1, &l->value)) != 0)
                FAIL(status);
            break;
        case DRES_TYPE_DRESVAR:
            dres_name(dres, l->value.v.id, varname, sizeof(varname));
            value = dres_scope_getvar(dres->scope, varname + 1);
            if ((status = dres_scope_setvar(scope, name + 1, value)) != 0)
                FAIL(status);
            break;
        default:
            FAIL(EINVAL);
        }
    }
    
    scope->prev = dres->scope;
    dres->scope = scope;

    return 0;

 fail:
    if (scope && scope->curr)
        dres_store_destroy(scope->curr);
    FREE(scope);
    
    return status;
#undef FAIL
}


/********************
 * dres_scope_pop
 ********************/
EXPORTED int
dres_scope_pop(dres_t *dres)
{
    dres_scope_t *prev;
    
    if (dres->scope == NULL)
        return EINVAL;
    
    prev = dres->scope->prev;
    
    if (dres->scope->names)
        g_hash_table_destroy(dres->scope->names);

    FREE(dres->scope);
    
    dres->scope = prev;

    return 0;
}


/********************
 * dres_scope_setvar
 ********************/
EXPORTED int
dres_scope_setvar(dres_scope_t *scope, char *name, dres_value_t *value)
{
#define FAIL(ec) do { err = (ec); goto fail; } while (0)

    char         *key = NULL;
    dres_value_t *val = NULL;
    int           err;

    DEBUG(DBG_VAR, "setting local variable %s in scope %p", name, scope);

    if ((key = STRDUP(name)) == NULL)
        FAIL(ENOMEM);
    
    if ((val = dres_copy_value(value)) == NULL)
        FAIL(ENOMEM);
    
    g_hash_table_insert(scope->names, key, val);
    
    return 0;

 fail:
    if (key)
        FREE(key);
    if (val)
        dres_free_value(val);
    return err;

#undef FAIL
}


/********************
 * dres_scope_getvar
 ********************/
EXPORTED dres_value_t *
dres_scope_getvar(dres_scope_t *scope, char *name)
{
    dres_value_t *value;
    
    DEBUG(DBG_VAR, "looking up local variable %s in scope %p", name, scope);

    if (scope == NULL || scope->names == NULL)
        return NULL;
    
    value = (dres_value_t *)g_hash_table_lookup(scope->names, name);
    
    return value;
}


/********************
 * dres_scope_push_args
 ********************/
int
dres_scope_push_args(dres_t *dres, char **locals)
{
    char          *name;
    dres_value_t value;
    int          i, status;

    if (locals == NULL)
        return 0;
    
    if ((status = dres_scope_push(dres, NULL)) != 0)
        return status;
    
    value.type = DRES_TYPE_STRING;
    for (i = 0; (name = locals[i]) != NULL; i += 2) {
        if ((value.v.s = locals[i + 1]) == NULL ||
            (status = dres_scope_setvar(dres->scope, name, &value)) != 0)
            goto fail;
    }

    return 0;
    
 fail:
    dres_scope_pop(dres);
    return status;
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
