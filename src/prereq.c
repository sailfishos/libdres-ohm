/*************************************************************************
This file is part of dres the resource policy dependency resolver.

Copyright (C) 2010 Nokia Corporation.

This library is free software; you can redistribute
it and/or modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation
version 2.1 of the License.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
USA.
*************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dres/dres.h>

/*****************************************************************************
 *                  *** prerequisite/dependency handling ***                 *
 *****************************************************************************/

/********************
 * dres_add_prereq
 ********************/
int
dres_add_prereq(dres_prereq_t *dep, int id)
{
    int i;
    
    if (dep->nid < 0)                              /* unmark as not present */
        dep->nid = 0;

    for (i = 0; i < dep->nid; i++)                 /* check if already there */
        if (dep->ids[i] == id)
            return 0;
    
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
