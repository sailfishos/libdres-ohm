#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dres/dres.h>
#include <dres/compiler.h>
#include "dres-debug.h"


/*****************************************************************************
 *                            *** target handling ***                        *
 *****************************************************************************/

/********************
 * dres_add_target
 ********************/
int
dres_add_target(dres_t *dres, char *name)
{
    dres_target_t *target;
    int            id;


    if (REALLOC_ARR(dres->targets, dres->ntarget, dres->ntarget + 1) == NULL)
        return DRES_ID_NONE;

    id     = dres->ntarget++;
    target = dres->targets + id;
    
    target->id   = DRES_UNDEFINED(DRES_TARGET(id));
    target->name = STRDUP(name);

    return target->name ? target->id : DRES_ID_NONE;
}


/********************
 * dres_target_id
 ********************/
int
dres_target_id(dres_t *dres, char *name)
{
    dres_target_t *target;
    unsigned int   i;

    if (name != NULL)
        for (i = 0, target = dres->targets; i < dres->ntarget; i++, target++) {
            if (!strcmp(name, target->name))
                return target->id;
        }
    
    return dres_add_target(dres, name);
}


/********************
 * dres_lookup_target
 ********************/
dres_target_t *
dres_lookup_target(dres_t *dres, char *name)
{
    dres_target_t *target;
    unsigned int   i;
    int            id;
    
    for (i = 0, target = dres->targets; i < dres->ntarget; i++, target++)
        if (!strcmp(name, target->name))
            return target;
    
    if ((id = dres_target_id(dres, name)) == DRES_ID_NONE)
        return NULL;
    else
        return dres->targets + DRES_INDEX(id);
}


/********************
 * dres_free_targets
 ********************/
void
dres_free_targets(dres_t *dres)
{
    dres_target_t *target;
    unsigned int   i;

    for (i = 0, target = dres->targets; i < dres->ntarget; i++, target++) {
        FREE(target->name);
        dres_free_prereq(target->prereqs);
        dres_free_actions(target->actions);
        FREE(target->dependencies);
    }

    FREE(dres->targets);
    dres->targets = NULL;
    dres->ntarget = 0;
}


/********************
 * dres_dump_targets
 ********************/
EXPORTED void
dres_dump_targets(dres_t *dres)
{
    dres_target_t *t;
    dres_prereq_t *d;
    dres_action_t *a;
    unsigned int   i, j;
    int            id, idx;
    char          *sep, name[64];

    
    printf("Found %d targets:\n", dres->ntarget);

    for (i = 0, t = dres->targets; i < dres->ntarget; i++, t++) {
        printf("target #%d: %s (0x%x)\n", i, t->name, t->id);
        if ((d = t->prereqs) != NULL) {
            printf("  depends on: ");
            for (j = 0, sep = ""; j < d->nid; j++, sep=", ") {
                dres_name(dres, d->ids[j], name, sizeof(name));
                printf("%s%s", sep, name);
            }
            printf("\n");

            printf("  updated as:");
            if (t->dependencies) {
                for (id = t->dependencies[j=0], sep = " ";
                     id != DRES_ID_NONE;
                     id = t->dependencies[++j], sep = ", ") {
                    idx = DRES_INDEX(id);
                    dres_name(dres, id, name, sizeof(name));
                    printf("%s%s", sep, name);
                }
                printf("\n");
            }
            else
                printf(" still unresolved...\n");
        }
        
        if (t->actions == NULL)
            printf("  no actions\n");
        else {
            printf("  actions:\n");
            for (a = t->actions; a; a = a->next)
                dres_dump_action(dres, a);
        }
    }
}


/********************
 * dres_check_target
 ********************/
int
dres_check_target(dres_t *dres, int tid)
{
    dres_target_t *target, *t;
    dres_prereq_t *prq;
    int            id, update, status;
    unsigned int   i;
    char           buf[32];

    DEBUG(DBG_RESOLVE, "checking target %s",
          dres_name(dres, tid, buf, sizeof(buf)));

    target = dres->targets + DRES_INDEX(tid);
    
    if ((prq = target->prereqs) == NULL)
        update = TRUE;
    else {
        update = FALSE;
        for (i = 0; i < prq->nid; i++) {
            id = prq->ids[i];
            switch (DRES_ID_TYPE(id)) {
            case DRES_TYPE_FACTVAR:
                if (dres_check_factvar(dres, id, target->stamp)) {
                    DEBUG(DBG_RESOLVE, "=> newer, %s needs to be updated",
                          target->name);
                    update = TRUE;
                }
                break;
            case DRES_TYPE_DRESVAR:
                if (dres_check_dresvar(dres, id, target->stamp)) {
                    DEBUG(DBG_RESOLVE, "=> newer, %s needs to be updated",
                          target->name);
                    update = TRUE;
                }
                break;
            case DRES_TYPE_TARGET:
                t = dres->targets + DRES_INDEX(id);
                DEBUG(DBG_RESOLVE, "%s: %d > %s: %d ?",
                      target->name, target->stamp, t->name, t->stamp);
                if (t->stamp > target->stamp) {
                    DEBUG(DBG_RESOLVE, "=> %s newer, %s needs to be updated",
                          t->name, target->name);
                    update = TRUE;
                }
                break;
            default:
                printf("*** BUG: invalid prereq 0x%x for %s ***\n",
                       id, target->name);
                break;
            }
        }
    }
    
    status = 0;

    if (update)
        if ((status = dres_run_actions(dres, target)) == 0)
            dres_update_target_stamp(dres, target);
    
    return status;
}







/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
