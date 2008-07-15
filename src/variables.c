#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <glib.h>
#include <glib-object.h>

#include <ohm/ohm-fact.h>

#include <dres/dres.h>
#include <dres/compiler.h>
#include "dres-debug.h"


/********************
 * dres_store_init
 ********************/
int
dres_store_init(dres_t *dres)
{
    OhmFactStore *fs;
    GHashTable   *ht;

    if ((fs = ohm_get_fact_store()) == NULL)
        return ENOENT;
    
    if ((ht = g_hash_table_new_full(g_str_hash,g_str_equal,NULL,NULL)) == NULL)
        return ENOMEM;
    
    dres->store.fs   = fs;
    dres->store.ht   = ht;
    dres->store.view = NULL;

    g_object_ref(fs);
    
    return 0;
}


/********************
 * dres_store_free
 ********************/
void
dres_store_free(dres_t *dres)
{
    dres_store_t *store = &dres->store;
    
    if (store->ht != NULL) {
        g_hash_table_destroy(store->ht);
        store->ht = NULL;
    }
    
    if (store->view != NULL) {
        g_object_unref(store->view);
        store->view = NULL;
    }

    if (store->fs) {
        g_object_unref(store->fs);
        store->fs = NULL;
    }
}


/********************
 * dres_store_track
 ********************/
int
dres_store_track(dres_t *dres)
{
    dres_store_t    *store = &dres->store;
    dres_variable_t *var;
    OhmPattern      *pattern;
    char            *name;
    int              id, i;
    
    if ((store->view = ohm_fact_store_new_view(store->fs, NULL)) == NULL)
        return ENOMEM;
    
    for (i = 0; i < dres->ndresvar; i++) {
        var  = dres->factvars + i;
        id   = var->id;
        name = var->name;

        if (!DRES_TST_FLAG(var, VAR_PREREQ))
            continue;
        
        if ((pattern = ohm_pattern_new(name)) == NULL)
            return ENOMEM;
        
        ohm_fact_store_view_add(store->view, OHM_STRUCTURE(pattern));
        g_hash_table_insert(store->ht, (gpointer)name, (gpointer)id);
    }

    return 0;
}


/********************
 * dres_store_check
 ********************/
int
dres_store_check(dres_t *dres)
{
    dres_store_t    *store = &dres->store;
    dres_variable_t *var;
    const char      *name;
    int              id, idx, updated;
    GSList          *changes, *l;
    OhmFact         *fact;
    OhmPatternMatch *match;


    if (store->view == NULL)
        return ENOENT;
    
    updated = FALSE;
    if ((changes = ohm_view_get_changes(store->view)) != NULL) {
        for (l = changes; l != NULL; l = g_slist_next(l)) {
            if (!OHM_PATTERN_IS_MATCH(l->data)) {
                printf("*** %s: invalid data from view\n", __FUNCTION__);
                continue;
            }
            
            match = OHM_PATTERN_MATCH(l->data);
            fact  = ohm_pattern_match_get_fact(match);
            name  = ohm_structure_get_name(OHM_STRUCTURE(fact));
            id    = (int)g_hash_table_lookup(store->ht, name);

#if 0
            printf("*** variable '%s' has changed\n", name);
#endif

            if (!id) {
                printf("*** %s: unkown variable %s\n", __FUNCTION__, name);
                continue;
            }

            if (DRES_ID_TYPE(id) != DRES_TYPE_FACTVAR) {
                printf("*** %s: got invalid type for variable %s (0x%x)\n",
                       __FUNCTION__, name, id);
                continue;
            }
            
            if ((idx = DRES_INDEX(id)) >= dres->nfactvar) {
                printf("*** %s: invalid index %d for variable %s\n",
                       __FUNCTION__, idx, name);
                continue;
            }
            
            var = dres->factvars + idx;
            dres_update_var_stamp(dres, var);
            
            updated = TRUE;
        }

        ohm_view_reset_changes(store->view);
    }
    
    return updated;
}


int
dres_store_tx_new(dres_t *dres)
{
    dres_store_t *store = &dres->store;

    ohm_fact_store_transaction_push(store->fs);
    DRES_SET_FLAG(dres, TRANSACTION_ACTIVE);

    DEBUG(DBG_VAR, "created new transaction");

    return TRUE;
}


int
dres_store_tx_commit(dres_t *dres)
{
    dres_store_t *store = &dres->store;
    
    ohm_fact_store_transaction_pop(store->fs, FALSE);
    DRES_CLR_FLAG(dres, TRANSACTION_ACTIVE);

    DEBUG(DBG_VAR, "committed transaction");

    return TRUE;
}


int
dres_store_tx_rollback(dres_t *dres)
{
    dres_store_t    *store = &dres->store;
    dres_target_t   *t;
    dres_variable_t *var;
    int              i;

    ohm_fact_store_transaction_pop(store->fs, TRUE);
    DRES_CLR_FLAG(dres, TRANSACTION_ACTIVE);

    for (i = 0, t = dres->targets; i < dres->ntarget; i++, t++)
        if (t->txid == dres->txid)
            t->stamp = t->txstamp;

    for (i = 0, var = dres->dresvars; i < dres->ndresvar; i++, var++)
        if (var->txid == dres->txid)
            var->stamp = var->txstamp;

    DEBUG(DBG_VAR, "rolled back transaction");
    
    return TRUE;
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
