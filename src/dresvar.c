#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <dres/dres.h>
#include "dres-debug.h"

/*****************************************************************************
 *                          *** variable handling ***                        *
 *****************************************************************************/

/********************
 * dres_add_dresvar
 ********************/
int
dres_add_dresvar(dres_t *dres, char *name)
{
    dres_variable_t *var;
    int              id;

    if (!REALLOC_ARR(dres->dresvars, dres->ndresvar, dres->ndresvar + 1))
        return DRES_ID_NONE;

    id  = dres->ndresvar++;
    var = dres->dresvars + id;

    var->id   = DRES_DRESVAR(id);
    var->name = STRDUP(name);

    return var->name ? var->id : DRES_ID_NONE;
}


/********************
 * dres_dresvar_id
 ********************/
int
dres_dresvar_id(dres_t *dres, char *name)
{
    dres_variable_t *var;
    int              i;

    if (name != NULL)
        for (i = 0, var = dres->dresvars; i < dres->ndresvar; i++, var++) {
            if (!strcmp(name, var->name))
                return var->id;
        }
    
    return dres_add_dresvar(dres, name);
}


/********************
 * dres_dresvar_name
 ********************/
const char *
dres_dresvar_name(dres_t *dres, int id)
{
    dres_variable_t *var = NULL;
    int              idx = DRES_INDEX(id);
    
    if (0 <= idx && idx <= dres->ndresvar)
        var = dres->dresvars + idx;

    return var ? var->name : NULL;
}


/********************
 * dres_free_dresvars
 ********************/
void
dres_free_dresvars(dres_t *dres)
{
    int              i;
    dres_variable_t *var;

    for (i = 0, var = dres->dresvars; i < dres->ndresvar; i++, var++) {
        FREE(var->name);
#if 0
        if (var->var)
            dres_var_destroy(var->var);
#endif
    }
    
    FREE(dres->dresvars);

    dres->dresvars = NULL;
    dres->ndresvar = 0;
}



/********************
 * dres_check_dresvar
 ********************/
int
dres_check_dresvar(dres_t *dres, int id, int refstamp)
{
    dres_variable_t *var = dres->dresvars + DRES_INDEX(id);
    char             name[64];
    
    DEBUG(DBG_RESOLVE, "%s: %d > %d ?",
          dres_name(dres, id, name, sizeof(name)), var->stamp, refstamp);
    
#ifdef STAMP_FORCED_UPDATE
    var->stamp = dres->stamp + 1;       /* fake that variables have changed */
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
