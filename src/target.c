#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dres/dres.h>


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
    int            i;

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
    int            i, id;
    
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
    int            i;

    for (i = 0, target = dres->targets; i < dres->ntarget; i++, target++) {
        FREE(target->name);
        dres_free_prereq(target->prereqs);
        dres_free_actions(target->actions);
    }

    FREE(dres->targets);
    dres->targets = NULL;
    dres->ntarget = 0;
}


/********************
 * dres_dump_targets
 ********************/
void
dres_dump_targets(dres_t *dres)
{
    int            i, j, id, idx;
    dres_target_t *t;
    dres_prereq_t *d;
    dres_action_t *a;
    dres_assign_t *v;
    char          *sep, lvalbuf[128], *lval, rvalbuf[128], *rval;
    int            n;
    
    printf("Found %d targets:\n", dres->ntarget);

    for (i = 0, t = dres->targets; i < dres->ntarget; i++, t++) {
        printf("target #%d: %s (0x%x, %p, %p)\n", i, t->name, t->id,
               t->prereqs, t->actions);
        if ((d = t->prereqs) != NULL) {
            for (j = 0; j < d->nid; j++) {
                id  = d->ids[j];
                idx = DRES_INDEX(id);
                switch (DRES_ID_TYPE(id)) {
                case DRES_TYPE_TARGET:
                    printf("  depends on target %s\n",
                           dres->targets[idx].name);
                    break;
                case DRES_TYPE_FACTVAR:
                    printf("  depends on FACT variable $%s\n",
                           dres->factvars[idx].name);
                    break;
                case DRES_TYPE_DRESVAR:
                    printf("  depends on DRES variable &%s\n",
                           dres->dresvars[idx].name);
                    break;
                default:
                    printf("  depends on unknown object 0x%x\n", id);
                }
            }
        }

        if (t->actions == NULL) {
            printf("  has no actions\n");
            continue;
        }
        
        printf("has actions:\n");
        for (a = t->actions; a; a = a->next) {
            lval = dres_dump_varref(dres, lvalbuf, sizeof(lvalbuf), &a->lvalue);
            rval = dres_dump_varref(dres, rvalbuf, sizeof(rvalbuf), &a->rvalue);
            if (lval)
                printf("  %s = ", lval);
            if (rval) {
                printf("%s\n", rval);
                continue;
            }
                
            printf("%s%s(", lval ? "" : "  ", a->name);
            for (n = 0, sep=""; n < a->nargument; n++, sep=",") {
                id  = a->arguments[n];
                idx = DRES_INDEX(id);
                switch (DRES_ID_TYPE(id)) {
                case DRES_TYPE_FACTVAR:
                    printf("%s$%s", sep, dres->factvars[idx].name);
                    break;
                case DRES_TYPE_DRESVAR:
                    printf("%s$%s", sep, dres->dresvars[idx].name);
                    break;
                case DRES_TYPE_LITERAL:
                    printf("%s%s", sep, dres->literals[idx].name);
                    break;
                default:
                    printf("%s<unknown>", sep);
                }
            }

            for (j = 0, v = a->variables; j < a->nvariable; j++, v++, sep=",") {
                char var[32], val[32];
                printf("%s%s=%s", sep,
                       dres_name(dres, v->var_id, var, sizeof(var)),
                       dres_name(dres, v->val_id, val, sizeof(val)));
            }
            
            printf(")\n");
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
    int            i, id, update;
    char           buf[32];

    DEBUG("checking target %s", dres_name(dres, tid, buf, sizeof(buf)));

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
                    DEBUG("=> newer, %s needs to be updated", target->name);
                    update = TRUE;
                }
                break;
            case DRES_TYPE_DRESVAR:
                if (dres_check_dresvar(dres, id, target->stamp)) {
                    DEBUG("=> newer, %s needs to be updated", target->name);
                    update = TRUE;
                }
                break;
            case DRES_TYPE_TARGET:
                t = dres->targets + DRES_INDEX(id);
                DEBUG("%s: %d > %s: %d ?",
                      target->name, target->stamp, t->name, t->stamp);
                if (t->stamp > target->stamp) {
                    DEBUG("=> %s newer, %s needs to be updates", t->name,
                          target->name);
                    update = TRUE;
                }
                break;
            default:
                DEBUG("### BUG: invalid prereq 0x%x for %s", id, target->name);
                break;
            }
        }
    }
        
    if (update) {
        dres_run_actions(dres, target);
        target->stamp = dres->stamp;
    }
    
    return update;
}







/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
