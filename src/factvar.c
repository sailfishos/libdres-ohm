#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <dres/dres.h>

/*****************************************************************************
 *                          *** variable handling ***                        *
 *****************************************************************************/

/********************
 * dres_add_variable
 ********************/
int
dres_add_variable(dres_t *dres, char *name)
{
    dres_variable_t *var;
    int              id;

    if (!REALLOC_ARR(dres->variables, dres->nvariable, dres->nvariable + 1))
        return DRES_ID_NONE;

    id  = dres->nvariable++;
    var = dres->variables + id;

    var->id   = DRES_VARIABLE(id);
    var->name = STRDUP(name);

    return var->name ? var->id : DRES_ID_NONE;
}


/********************
 * dres_variable_id
 ********************/
int
dres_variable_id(dres_t *dres, char *name)
{
    dres_variable_t *var;
    int              i;

    if (name != NULL)
        for (i = 0, var = dres->variables; i < dres->nvariable; i++, var++) {
            if (!strcmp(name, var->name))
                return var->id;
        }
    
    return dres_add_variable(dres, name);
}


/********************
 * dres_free_variables
 ********************/
void
dres_free_variables(dres_t *dres)
{
    int              i;
    dres_variable_t *var;

    for (i = 0, var = dres->variables; i < dres->nvariable; i++, var++) {
        FREE(var->name);
#if 0
        if (var->var)
            dres_var_destroy(var->var);
#endif
    }
    
    FREE(dres->variables);

    dres->variables = NULL;
    dres->nvariable = 0;
}



/********************
 * dres_check_variable
 ********************/
int
dres_check_variable(dres_t *dres, int id, int refstamp)
{
    dres_variable_t *var = dres->variables + DRES_INDEX(id);
    char             buf[32];
    
    DEBUG("%s: %d > %d ?", dres_name(dres, id, buf, sizeof(buf)),
          var->stamp, refstamp);
    
#ifdef STAMP_FORCED_UPDATE
    var->stamp = dres->stamp+1;          /* fake that variables have changed */
    return TRUE;
#endif
    
    return var->stamp > refstamp;
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
