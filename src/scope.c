#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <dres/dres.h>


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
int
dres_scope_push(dres_t *dres, dres_assign_t *variables, int nvariable)
{
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
}


/********************
 * dres_scope_pop
 ********************/
int
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
    
    FREE(dres->scope);
    
    dres->scope = prev;

    return 0;
}


/********************
 * dres_scope_setvar
 ********************/
int
dres_scope_setvar(dres_scope_t *scope, char *name, char *value)
{
    dres_var_t *var;
    char       *valuep;

    DEBUG("setting local variable %s=%s", name, value);

    valuep = value;
    if ((var = dres_var_init(scope->curr, name, NULL)) == NULL)
        return errno;
    if (!dres_var_set_field(var, DRES_VAR_FIELD, NULL, VAR_STRING, &valuep))
        return errno;
    
    return 0;
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
        if ((value = locals[i+1]) == NULL)
            goto fail;
        if ((status = dres_scope_setvar(dres->scope, name, &value)) != 0)
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
