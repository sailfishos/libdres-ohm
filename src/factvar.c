#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

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

    for (i = 0, var = dres->factvars; i < dres->nfactvar; i++, var++)
        FREE(var->name);
    
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
    char             name[64];
    int              touched;

    touched = var->stamp > refstamp;
    
    DEBUG(DBG_RESOLVE, "%s: %s (%d > %d)",
          dres_name(dres, id, name, sizeof(name)),
          touched ? "outdated" : "up-to-date",
          var->stamp, refstamp);
    
    return touched;
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


/********************
 * dres_save_factvars
 ********************/
int
dres_save_factvars(dres_t *dres, dres_buf_t *buf)
{
    dres_variable_t *v;
    int              i;

    dres_buf_ws32(buf, dres->nfactvar);
    buf->header.nvariable += dres->nfactvar;
    
    for (i = 0, v = dres->factvars; i < dres->nfactvar; i++, v++) {
        dres_buf_ws32(buf, v->id);
        dres_buf_wstr(buf, v->name);
        dres_buf_wu32(buf, v->flags);
    }
    
    return 0;
}


/********************
 * dres_load_factvars
 ********************/
int
dres_load_factvars(dres_t *dres, dres_buf_t *buf)
{
    dres_variable_t *v;
    int              i;

    dres->nfactvar = dres_buf_rs32(buf);
    dres->factvars = dres_buf_alloc(buf,dres->nfactvar*sizeof(*dres->factvars));
    
    if (dres->factvars == NULL)
        return ENOMEM;
    
    for (i = 0, v = dres->factvars; i < dres->nfactvar; i++, v++) {
        v->id    = dres_buf_rs32(buf);
        v->name  = dres_buf_rstr(buf);
        v->flags = dres_buf_ru32(buf);
    }
    
    return buf->error;
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
