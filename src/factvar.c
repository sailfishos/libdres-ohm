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
 * dres_add_factvar
 ********************/
int
dres_add_factvar(dres_t *dres, char *name)
{
    dres_variable_t *var;
    int              id;

    if (!REALLOC_ARR(dres->factvars, dres->nfactvar, dres->nfactvar + 1))
        return DRES_ID_NONE;

    id  = dres->nfactvar++;
    var = dres->factvars + id;

    var->id   = DRES_FACTVAR(id);
    var->name = STRDUP(name);

    return var->name ? var->id : DRES_ID_NONE;
}


/********************
 * dres_factvar_id
 ********************/
int
dres_factvar_id(dres_t *dres, char *name)
{
    dres_variable_t *var;
    int              i;

    if (name != NULL)
        for (i = 0, var = dres->factvars; i < dres->nfactvar; i++, var++) {
            if (!strcmp(name, var->name))
                return var->id;
        }
    
    return dres_add_factvar(dres, name);
}


/********************
 * dres_factvar_name
 ********************/
const char *
dres_factvar_name(dres_t *dres, int id)
{
    dres_variable_t *var = NULL;
    int              idx = DRES_INDEX(id);
    
    if (0 <= idx && idx <= dres->nfactvar)
        var = dres->factvars + idx;
    
    return var ? var->name : NULL;
}


/********************
 * dres_free_factvars
 ********************/
void
dres_free_factvars(dres_t *dres)
{
    int              i;
    dres_variable_t *var;

    for (i = 0, var = dres->factvars; i < dres->nfactvar; i++, var++) {
        FREE(var->name);
#if 0
        if (var->var)
            dres_var_destroy(var->var);
#endif
    }
    
    FREE(dres->factvars);

    dres->factvars = NULL;
    dres->nfactvar = 0;
}



/********************
 * dres_check_factvar
 ********************/
int
dres_check_factvar(dres_t *dres, int id, int refstamp)
{
    dres_variable_t *var = dres->factvars + DRES_INDEX(id);
    char             buf[32];
    
    DEBUG(DBG_RESOLVE, "%s: %d > %d ?",
          dres_name(dres, id, buf, sizeof(buf)), var->stamp, refstamp);
    
#ifdef STAMP_FORCED_UPDATE
    var->stamp = dres->stamp+1;          /* fake that variables have changed */
    return TRUE;
#endif
    
    return var->stamp > refstamp;
}


/********************
 * dres_dump_init
 ********************/
void
dres_dump_init(dres_t *dres)
{
    dres_initializer_t *init;
    dres_init_t        *i;
    char                var[128], *t;
    
    for (init = dres->initializers; init != NULL; init = init->next) {
        dres_name(dres, init->variable, var, sizeof(var));
        printf("%s = {", var);
        for (i = init->fields, t = " "; i != NULL; i = i->next, t = ", ") {
            printf("%s%s: ", t, i->field.name);
            switch (i->field.value.type) {
            case DRES_TYPE_INTEGER: printf("%d", i->field.value.v.i);   break;
            case DRES_TYPE_DOUBLE:  printf("%f", i->field.value.v.d);   break;
            case DRES_TYPE_STRING:  printf("'%s'", i->field.value.v.s); break;
            default:                printf("<unknown>");
            }
        }
        printf(" }\n");
        fflush(stdout);
    }
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
