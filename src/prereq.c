#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dres/dres.h>

#define NOT_IN_GRAPH ((unsigned int)-1)

/*****************************************************************************
 *                  *** prerequisite/dependency handling ***                 *
 *****************************************************************************/

/********************
 * dres_add_prereq
 ********************/
int
dres_add_prereq(dres_prereq_t *dep, int id)
{
    if (dep->nid == NOT_IN_GRAPH)                   /* unmark as not present */
        dep->nid = 0;

    if (REALLOC_ARR(dep->ids, dep->nid, dep->nid + 1) == NULL)
        return DRES_ID_NONE;

    dep->ids[dep->nid++] = id;

    return 0;
}


/********************
 * dres_new_prereq
 ********************/
dres_prereq_t *
dres_new_prereq(int id)
{
    dres_prereq_t *dep;

    if (ALLOC_OBJ(dep) == NULL)
        return NULL;
    
    if (dres_add_prereq(dep, id) != DRES_ID_NONE)
        return dep;

    dres_free_prereq(dep);
    return NULL;
}


/********************
 * dres_free_prereq
 ********************/
void
dres_free_prereq(dres_prereq_t *dep)
{
    if (dep) {
        FREE(dep->ids);
        FREE(dep);
    }
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
