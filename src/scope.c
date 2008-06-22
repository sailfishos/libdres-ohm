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
 * free_var
 ********************/
static void
free_var(gpointer ptr)
{
    dres_var_t *var = (dres_var_t *)ptr;

    dres_var_destroy(var);
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
dres_scope_push(dres_t *dres, dres_assign_t *variables, int nvariable)
{
#if 1

#define FAIL(err) do { status = err; goto fail; } while (0)
    dres_scope_t  *scope;
    dres_assign_t *a;
    char           name[64], value[64], *valuep;
    int            i, status;
    
    
    if (ALLOC_OBJ(scope) == NULL)
        FAIL(ENOMEM);

    if ((scope->curr = dres_store_init(STORE_LOCAL, NULL)) == NULL)
        FAIL(ENOMEM);

    scope->names = g_hash_table_new_full(g_str_hash, g_str_equal,
                                         free_name, free_var);
    if (scope->names == NULL)
        FAIL(ENOMEM);
                                         

    for (i = 0, a = variables; i < nvariable; i++, a++) {
        
        if (a->lvalue.selector != NULL || a->lvalue.field != NULL)
            FAIL(EINVAL);
        
        dres_name(dres, a->lvalue.variable, name, sizeof(name));

        switch (a->type) {
        case DRES_ASSIGN_IMMEDIATE:
            dres_name(dres, a->val, value, sizeof(value));
            if ((status = dres_scope_setvar(scope, name + 1, value)) != 0)
                FAIL(status);
            break;

        case DRES_ASSIGN_VARIABLE:
            if (DRES_ID_TYPE(a->var.variable) != DRES_TYPE_DRESVAR)
                FAIL(EINVAL);
            
            dres_name(dres, a->var.variable, value, sizeof(value));

            if ((valuep = dres_scope_getvar(dres->scope, value + 1)) == NULL)
                valuep = STRDUP("");
            status = dres_scope_setvar(scope, name + 1, valuep);

            FREE(valuep);
            if (status != 0)
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

#else

#define FAIL(err) do { status = err; goto fail; } while (0)
    dres_scope_t  *scope;
    dres_assign_t *a;
    dres_var_t    *var;
    char           name[64], value[64], *namep;
    int            i, status;
    
    
    if (ALLOC_OBJ(scope) == NULL)
        FAIL(ENOMEM);

    if ((scope->curr = dres_store_init(STORE_LOCAL, NULL)) == NULL)
        FAIL(ENOMEM);

    for (i = 0, a = variables; i < nvariable; i++, a++) {
        dres_name(dres, a->var_id, name, sizeof(name));
        dres_name(dres, a->val_id, value, sizeof(value));
        if ((status = dres_scope_setvar(scope, name + 1, value)) != 0)
            FAIL(status);
    }
    
    scope->prev = dres->scope;
    dres->scope = scope;

    return 0;

 fail:
    if (scope && scope->curr)
        dres_store_destroy(scope->curr);
    FREE(scope);
    
    return status;
#endif
}


/********************
 * dres_scope_pop
 ********************/
EXPORTED int
dres_scope_pop(dres_t *dres)
{
    dres_scope_t *prev;
    dres_store_t *curr;
    
    if (dres->scope == NULL)
        return EINVAL;
    
    prev = dres->scope->prev;
    curr = dres->scope->curr;
    
    if (curr)
        dres_store_destroy(curr);
    
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
dres_scope_setvar(dres_scope_t *scope, char *name, char *value)
{
    dres_var_t *var;
    char       *key, *valuep;

    DEBUG(DBG_VAR, "setting local variable %s=%s in scope %p",
          name, value, scope);

    if ((key = STRDUP(name)) == NULL)
        return ENOMEM;
    
    valuep = value;
    if ((var = dres_var_init(scope->curr, name, NULL)) == NULL)
        goto fail;
    if (!dres_var_set_field(var, DRES_VAR_FIELD, NULL, VAR_STRING, &valuep))
        goto fail;
    
    g_hash_table_insert(scope->names, key, var);
    
    return 0;

 fail:
    if (key)
        FREE(key);

    return EINVAL;
}


/********************
 * dres_scope_getvar
 ********************/
EXPORTED char *
dres_scope_getvar(dres_scope_t *scope, char *name)
{
    dres_var_t *var;
    char       *value;
    
    DEBUG(DBG_VAR, "looking up local variable %s in scope %p", name, scope);

    if (scope == NULL || scope->names == NULL)
        return NULL;
    
    if ((var = (dres_var_t *)g_hash_table_lookup(scope->names, name)) == NULL)
        return NULL;

    if (dres_var_get_field(var, DRES_VAR_FIELD, NULL, VAR_STRING, &value))
        return value;
    else
        return NULL;
}


/********************
 * dres_scope_push_args
 ********************/
int
dres_scope_push_args(dres_t *dres, char **locals)
{
    char *name, *value;
    int   i, status;

    if (locals == NULL)
        return 0;
    
    if ((status = dres_scope_push(dres, NULL, 0)) != 0)
        return status;

    for (i = 0; (name = locals[i]) != NULL; i += 2) {
        if ((value  = locals[i+1]) == NULL ||
            (status = dres_scope_setvar(dres->scope, name, value)) != 0)
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
