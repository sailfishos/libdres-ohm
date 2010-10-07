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
#include <errno.h>
#include <arpa/inet.h>

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

    /* don't create new vars if we resolve or run a precompiled file */
    if (DRES_TST_FLAG(dres, TARGETS_FINALIZED) || DRES_TST_FLAG(dres, COMPILED))
        return DRES_ID_NONE;
    
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
        dres_free_statement(target->statements);
        FREE(target->dependencies);
        vm_chunk_del(target->code);
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
    dres_stmt_t   *stmt;
    int            i, j, id, idx;
    char          *sep, name[64];
    char           buf[16384];

    
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

        printf("  statements:\n");
        if (t->statements)
            for (stmt = t->statements; stmt; stmt = stmt->any.next)
                dres_dump_statement(dres, stmt, 4);
        else
            printf("    none\n");
        
        if (t->code == NULL) {
            if (t->statements != NULL)
                printf("  byte code not generated\n");
        }
        else {
            vm_state_t vm;
            int        indent = 4;
    
            vm.chunk  = t->code;
            vm.pc     = t->code->instrs;
            vm.ninstr = t->code->ninstr;
            vm.nsize  = t->code->nsize;
            vm_dump_chunk(&vm, buf, sizeof(buf), indent);

            printf("  byte code:\n");
            printf("%s", buf);
        }

    }
    fflush(stdout);
}


/********************
 * dres_check_target
 ********************/
int
dres_check_target(dres_t *dres, int tid)
{
    dres_target_t *target, *t;
    dres_prereq_t *prq;
    int            i, id, update, status;
    char           buf[32];

    DEBUG(DBG_RESOLVE, "checking target %s",
          dres_name(dres, tid, buf, sizeof(buf)));

    target = dres->targets + DRES_INDEX(tid);
    
    if ((prq = target->prereqs) == NULL) {
        DEBUG(DBG_RESOLVE, "no prereqs (always update)");
        update = TRUE;
    }
    else {
        update = FALSE;
        for (i = 0; i < prq->nid; i++) {
            id = prq->ids[i];
            switch (DRES_ID_TYPE(id)) {
            case DRES_TYPE_FACTVAR:
                if (dres_check_factvar(dres, id, target->stamp))
                    update = TRUE;
                break;
            case DRES_TYPE_DRESVAR:
                if (dres_check_dresvar(dres, id, target->stamp))
                    update = TRUE;
                break;
            case DRES_TYPE_TARGET:
                t = dres->targets + DRES_INDEX(id);
                DEBUG(DBG_RESOLVE, "%s: %s (%d > %d)",
                      t->name,
                      t->stamp > target->stamp ? "outdated" : "up-to-date",
                      t->stamp, target->stamp);
                if (t->stamp > target->stamp)
                    update = TRUE;
                break;
            default:
                DRES_ERROR("BUG: invalid prereq 0x%x for %s", id, target->name);
                break;
            }
        }
    }
    
    if (update) {
        DEBUG(DBG_RESOLVE, "=> %s needs to be updated", target->name);
        if ((status = dres_run_actions(dres, target)) > 0)
            dres_update_target_stamp(dres, target);
    }
    else {
        DEBUG(DBG_RESOLVE, "=> %s already up-to-date", target->name);
        status = TRUE;
    }
    
    return status;
}


/********************
 * dres_save_targets
 ********************/
int
dres_save_targets(dres_t *dres, dres_buf_t *buf)
{
    dres_target_t *t;
    int            i, j;

    dres_buf_ws32(buf, dres->ntarget);
    buf->header.ntarget = dres->ntarget;

    for (i = 0, t = dres->targets; i < dres->ntarget; i++, t++) {
        dres_buf_ws32(buf, t->id);
        dres_buf_wstr(buf, t->name);
        if (t->prereqs == NULL)
            dres_buf_ws32(buf, 0);
        else {
            dres_buf_ws32(buf, t->prereqs->nid);
            
            for (j = 0; j < t->prereqs->nid; j++) {
                dres_buf_ws32(buf, t->prereqs->ids[j]);
                buf->header.ntarget++;
            }
        }

        /* statements skipped */
        
        if (t->code == NULL) {
            dres_buf_ws32(buf, 0);
        }
        else {
            dres_buf_ws32(buf, t->code->ninstr);
            dres_buf_ws32(buf, t->code->nsize);
            
#if 1
            dres_buf_wbuf(buf, (char *)t->code->instrs, t->code->nsize);
#else /* XXX TODO: this is completely broken now wrt. endianness */
            for (j = 0; j < t->code->ninstr; j++)
                dres_buf_wu32(buf, t->code->instrs[j]);
#endif

            buf->header.ncode++;
            buf->header.sinstr += t->code->nsize;
        }
        
        if (t->dependencies == NULL)
            dres_buf_ws32(buf, 0);
        else {
            for (j = 0; t->dependencies[j] != DRES_ID_NONE; j++)
                ;
            dres_buf_ws32(buf, j + 1);

            for (j = 0; t->dependencies[j] != DRES_ID_NONE; j++)
                dres_buf_ws32(buf, t->dependencies[j]);

            buf->header.ndependency += j + 1;
        }
    }

    return buf->error;
}


/********************
 * dres_load_targets
 ********************/
int
dres_load_targets(dres_t *dres, dres_buf_t *buf)
{
    dres_target_t *t;
    int            i, j, n;

    dres->ntarget = dres_buf_rs32(buf);
    dres->targets = dres_buf_alloc(buf, dres->ntarget * sizeof(*dres->targets));

    if (dres->targets == NULL)
        return ENOMEM;

    for (i = 0, t = dres->targets; i < dres->ntarget; i++, t++) {
        t->id   = dres_buf_rs32(buf);
        t->name = dres_buf_rstr(buf);
        n       = dres_buf_rs32(buf);

        if (n <= 0)
            t->prereqs = NULL;
        else {
            t->prereqs = dres_buf_alloc(buf, sizeof(*t->prereqs));

            if (t->prereqs == NULL)
                return ENOMEM;
            
            t->prereqs->nid = n;
            t->prereqs->ids = dres_buf_alloc(buf, n * sizeof(*t->prereqs->ids));

            if (t->prereqs->ids == NULL)
                return ENOMEM;

            for (j = 0; j < t->prereqs->nid; j++)
                t->prereqs->ids[j] = dres_buf_rs32(buf);
        }

        /* statements skipped */
        
        n = dres_buf_rs32(buf);
        
        if (n <= 0)
            t->code = NULL;
        else {
            t->code = dres_buf_alloc(buf, sizeof(*t->code));
            
            if (!DRES_ALIGNED_OK((ptrdiff_t)t->code)) {
                DRES_ERROR("%s: VM code alignment error (%p) for target '%s'.",
                           __FUNCTION__, t->code, t->name);
                exit(1);
            }

            t->code->ninstr = n;
            t->code->nsize  = dres_buf_rs32(buf);
            
            if (t->code == NULL)
                return ENOMEM;

#if 1 /* XXX TODO: this is broken wrt. endianness */
            t->code->instrs = (unsigned int *)dres_buf_rbuf(buf,t->code->nsize);
#else /* XXX TODO: this is also broken wrt. endianness */
            t->code->instrs =
                dres_buf_alloc(buf, t->code->ninstr * sizeof(*t->code->instrs));
            
            if (t->code->instrs == NULL)
                return ENOMEM;
            
            for (j = 0; j < t->code->ninstr; j++)
                t->code->instrs[j] = dres_buf_ru32(buf);
#endif
        }
        
        n = dres_buf_rs32(buf);

        if (n <= 0)
            t->dependencies = NULL;
        else {
            t->dependencies = dres_buf_alloc(buf, n * sizeof(*t->dependencies));
            
            if (t->dependencies == NULL)
                return ENOMEM;
                                             
            for (j = 0; j < n - 1; j++)
                t->dependencies[j] = dres_buf_rs32(buf);
            t->dependencies[j] = DRES_ID_NONE;
        }
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
