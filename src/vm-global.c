#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "mm.h"
#include "vm.h"


/********************
 * vm_global_lookup
 ********************/
int
vm_global_lookup(char *name, vm_global_t **gp)
{
    vm_global_t  *g;
    OhmFactStore *fs = ohm_fact_store_get_fact_store();
    GSList       *fl;
    int           i, n;
    
    if ((fl = ohm_fact_store_get_facts_by_name(fs, name)) == NULL ||
        (n  = g_slist_length(fl)) == 0) {
        *gp = NULL;
        return ENOENT;
    }
    
    if (ALLOC_VAROBJ(g, n, facts) == NULL) {
        *gp = NULL;
        return ENOMEM;
    }
    
    g->nfact = 0;
    for (i = 0; i < n && fl; i++, fl = g_slist_next(fl)) {
        g->facts[i] = (OhmFact *)fl->data;
        g_object_ref(g->facts[i]);
        g->nfact++;
    }
    
    *gp = g;
    return 0;
}


/********************
 * vm_global_free
 ********************/
void
vm_global_free(vm_global_t *g)
{
    int i;

    for (i = 0; i < g->nfact; i++)
        if (g->facts[i])
            g_object_unref(g->facts[i]);

    FREE(g);
}








/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


