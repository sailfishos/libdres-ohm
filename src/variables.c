#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <glib.h>
#include <glib-object.h>

#include "prolog/ohm-fact.h"

#include <dres/dres.h>
#include <dres/variables.h>



/*
 * Storage
 */
#define STORAGE_COMMON_FIELDS   \
    dres_storetype_t   type;    \
    int                refcnt;  \
    char              *prefix;  \
    GHashTable        *htbl;    \
    OhmFactStore      *fs

typedef struct {
    STORAGE_COMMON_FIELDS;
} dres_any_store_t;

typedef struct {
    STORAGE_COMMON_FIELDS;
    OhmFactStoreView  *view;
    GSList            *patls;
    int                interested;
} dres_fact_store_t;

typedef struct {
    STORAGE_COMMON_FIELDS;
} dres_local_store_t;


union dres_store_u {
    dres_storetype_t   type;
    dres_any_store_t   any;
    dres_fact_store_t  fact;
    dres_local_store_t local;
}; /* dres_store_t */


/*
 * variables
 */
#define VARIABLE_COMMON_FIELDS   \
    dres_vartype_t     type;     \
    dres_store_t      *store

typedef struct {
    VARIABLE_COMMON_FIELDS;
} dres_any_var_t;

typedef struct {
    VARIABLE_COMMON_FIELDS;
    char              *name;
    int               *pstamp;
} dres_fstore_var_t;

typedef struct {
    VARIABLE_COMMON_FIELDS;
    OhmFact           *fact;
} dres_local_var_t;

union dres_var_u {
    dres_storetype_t   type;
    dres_any_var_t     any;
    dres_fstore_var_t  fact;
    dres_local_var_t   local;
}; /* dres_var_t */

static void free_list_elem(gpointer, gpointer);
static int  get_local_var(dres_local_var_t *, const char *,
                          dres_vartype_t, void *);
static int  get_fact_var(dres_fstore_var_t *, const char *,
                         dres_vartype_t, void *);



dres_store_t *dres_store_init(dres_storetype_t type, char *prefix)
{
    dres_store_t     *store;
    OhmFactStore     *fs;
    OhmFactStoreView *view;
    GHashTable       *htbl;
    char              buf[512];

    if (prefix == NULL)
        prefix = "";
    else {
        snprintf(buf, sizeof(buf), "%s.", prefix);
        prefix = buf;
    }

    if ((store = (dres_store_t *)malloc(sizeof(*store))) == NULL)
        goto failed;            /* errno = ENOMEM; */

    memset(store, 0, sizeof(*store));

    htbl = g_hash_table_new_full(g_str_hash, g_str_equal, free, NULL);

    if (!(store->any.htbl = htbl) || !(store->any.prefix = strdup(prefix))) {
        errno = ENOMEM;
        goto failed;
    }

    switch ((store->type = type)) {

    case STORE_FACT:
        if ((fs = ohm_fact_store_get_fact_store()) == NULL) {
            errno = ENOENT;
            goto failed;
        }
        if ((view = ohm_fact_store_new_view(fs, NULL)) == NULL) {
            errno = EIO;
            goto failed;
        }

        store->fact.fs   = fs;
        store->fact.view = view;

        break;

    case STORE_LOCAL:
        if ((fs = ohm_fact_store_new()) == NULL) {
            errno = EIO;
            goto failed;
        }

        store->local.fs = fs;

        break;

    default:
        errno = EINVAL;
        goto failed;
    }


    store->any.refcnt = 1;

    return store;

 failed:
    dres_store_destroy(store);
    return NULL;
}


void dres_store_destroy(dres_store_t *store)
{
    if (store != NULL) {
        if (--store->any.refcnt <= 0) {
            if (store->any.prefix != NULL)
                free(store->any.prefix);
            
            if (store->any.htbl != NULL)
                g_hash_table_destroy(store->any.htbl);
            

            switch (store->type) {

            case STORE_FACT:
                if (store->fact.view != NULL)
                    g_object_unref(store->fact.view);
                if (store->fact.patls != NULL) {
                    g_slist_foreach(store->fact.patls, free_list_elem, NULL);
                    g_slist_free(store->fact.patls);
                }
                break;

            case STORE_LOCAL:
                break;

            default:
                break;
            }

            if (store->any.fs != NULL)
                g_object_unref(store->any.fs);

            free(store);
        }
    }
}


void dres_store_finish(dres_store_t *store)
{
    dres_fact_store_t *fstore = &store->fact;
    OhmFactStoreView  *view;

    if (!store || store->type != STORE_FACT)
        return;

    if ((view = fstore->view) != NULL && !fstore->interested) {
        ohm_fact_store_view_set_interested(view, fstore->patls);
        fstore->interested = TRUE;
    }
}


int dres_store_check(dres_store_t *store, char *name)
{
    return ohm_fact_store_get_facts_by_name(store->fact.fs, name) != NULL;
}


void dres_store_update_timestamps(dres_store_t *store, int stamp)
{
    dres_fact_store_t     *fstore = &store->fact;
    OhmFactStoreView      *view;
    OhmFactStoreChangeSet *chset;
    OhmPatternMatch       *pm;
    OhmFact               *fact;
    GSList                *list;
    const char            *name;
    dres_fstore_var_t     *var;

    if (!store || store->type != STORE_FACT)
        return;

    if ((view = fstore->view) != NULL && fstore->interested) {
        if ((chset = view->change_set) != NULL) {
            for (list  = ohm_fact_store_change_set_get_matches(chset);
                 list != NULL;
                 list  = g_slist_next(list))
            {
                if (OHM_PATTERN_IS_MATCH(list->data)) {
                    pm   = OHM_PATTERN_MATCH(list->data);
                    fact = ohm_pattern_match_get_fact(pm);
                    name = ohm_structure_get_name(OHM_STRUCTURE(fact));
                    var  = g_hash_table_lookup(store->fact.htbl, name);

                    printf("***** updating stamp of %s to %d *****\n",
                           name, stamp);

                    if (var != NULL && var->pstamp != NULL)
                        *(var->pstamp) = stamp;
                }
            }

            ohm_fact_store_change_set_reset(chset);
        }
    }
}


dres_var_t *dres_var_init(dres_store_t *store, char *name, int *pstamp)
{
    dres_var_t *var = NULL;
    char        buf[512];
    OhmPattern *pat;
    OhmFact    *fact;
    GSList     *list;

    if (!store || !name || (store->type == STORE_FACT && !pstamp)) {
        errno = EINVAL;
        return NULL;
    }

    snprintf(buf, sizeof(buf), "%s%s", store->any.prefix, name);
    DEBUG("adding %s as %s", name, buf);
    name = strdup(buf);

    if ((var = (dres_var_t *)g_hash_table_lookup(store->any.htbl, name))) {
        return var;
    }


    if ((var = (dres_var_t *)malloc(sizeof(*var))) == NULL)
        return NULL;            /* errno = ENOMEM; */

    memset(var, 0, sizeof(*var));
    var->any.type   = VAR_UNDEFINED;
    var->any.store  = store;

    switch (store->type) {
        
    case STORE_FACT:
        if (store->fact.interested) {
            errno = EBUSY;
            goto failed;
        }
        if (!(list = ohm_fact_store_get_facts_by_name(store->fact.fs, name)) ||
            g_slist_length(list) == 0) {
            errno = ENOENT;
            goto failed;
        }
        if ((pat = ohm_pattern_new(name)) == NULL) {
            errno = EINVAL;
            goto failed;
        }
        store->fact.patls = g_slist_prepend(store->fact.patls, pat);
        var->fact.name    = strdup(name);
        var->fact.pstamp  = pstamp;
        break;

    case STORE_LOCAL:
        if ((fact = ohm_fact_new(name)) == NULL ||
            !ohm_fact_store_insert(store->fact.fs, fact)) {
            errno = EIO;
            goto failed;
        }
        var->local.fact = fact;
        break;

    default:
        errno = EINVAL;
        goto failed;
    }

    g_hash_table_insert(store->any.htbl, (gpointer)name, (gpointer)var);

    store->any.refcnt++;

    return var;

 failed:
    dres_var_destroy(var);
    return NULL;
}


void dres_var_destroy(dres_var_t *var)
{
    dres_store_t *store;

    if (var != NULL) {
        if ((store = var->any.store) != NULL) {
            switch (store->type) {

            case STORE_LOCAL:
                if (var->local.fact)
                    g_object_unref(var->local.fact);
                break;

            case STORE_FACT:
                if (var->fact.name)
                    free(var->fact.name);
                break;

            default:
                break;
            }
            dres_store_destroy(store);
        }

        free(var);
    }
}

int dres_var_set_value(dres_var_t *var, const char *name,
                       dres_vartype_t type, void *pval)
{
    static int   empty_int = 0;
    static char *empty_str = "";

    GValue gval;

    if (!var || !name || !pval) {
        errno = EINVAL;
        return FALSE;
    }

    if (!var->any.store || var->any.store->type != STORE_LOCAL) {
        errno = ENOSYS;
        return FALSE;
    }

    switch (type) {

    case VAR_INT:
        gval = ohm_value_from_int(pval ? *(int *)pval : empty_int);
        break;

    case VAR_STRING:
        gval = ohm_value_from_string(pval ? *(char **)pval : empty_str);
        break;

    default:
        type  = VAR_UNDEFINED;
        errno = EINVAL;
        return FALSE;
    }

    if ((var->type = type) != VAR_UNDEFINED)
        ohm_fact_set(var->local.fact, name, &gval);

    return TRUE;
}

int dres_var_get_value(dres_var_t *var, const char *name, 
                       dres_vartype_t type, void *pval)
{
    dres_store_t *store;
    int retval = FALSE;

    if (!var || !name || !pval)
        errno = EINVAL;
    else if ((store = var->any.store) == NULL)
        errno = ENOSYS;
    else {
        switch (store->type) {

        case STORE_LOCAL:
            retval = get_local_var(&var->local, name, type, pval);
            break;
            
        case STORE_FACT:
            retval = get_fact_var(&var->fact, name, type, pval);
            break;
            
        default:                    /* we should not really get ever here*/
            errno = EINVAL;
            break;
        }
    }

    return retval;
}

static void free_list_elem(gpointer data, gpointer user_data)
{
    g_object_unref(data);
}

static int get_fact_var(dres_fstore_var_t *var, const char *name,
                        dres_vartype_t type, void *pval)
{
    dres_fact_store_t *store;
    GSList            *list;
    GValue            *gval;
    OhmFact           *fact;
    dres_vartype_t     btyp;
    dres_array_t      *arr;
    int                llen;
    int                i;

    if (!var || !name || !pval) {
        errno = EINVAL;
        return FALSE;
    }

    if ((store = &var->store->fact) == NULL) {
        errno = EIO;
        return FALSE;
    }

    list = ohm_fact_store_get_facts_by_name(store->fs, var->name);
    llen = list ? g_slist_length(list) : 0;
    btyp = VAR_BASE_TYPE(type);
    arr  = NULL;

    if (VAR_IS_ARRAY(type)) {
        switch (btyp) {

        case VAR_INT:
            if ((arr = malloc(sizeof(*arr) + (llen * sizeof(int)))) == NULL)
                return FALSE;
            
            arr->len = llen;
            
            for (i = 0;    list != NULL;   i++, list = g_slist_next(list)) {
                fact = list->data;

                if ((gval = ohm_fact_get(fact, name)) == NULL) {
                    errno = EIO;
                    goto failed;
                }

                if (G_VALUE_TYPE(gval) != G_TYPE_INT) {
                    errno = EINVAL;
                    goto failed;
                }

                arr->integer[i] = g_value_get_int(gval);
            }
            break;

        case VAR_STRING:
            if ((arr = malloc(sizeof(*arr) + (llen * sizeof(char *)))) == NULL)
                return FALSE;
            
            arr->len = llen;
            
            for (i = 0;    list != NULL;   i++, list = g_slist_next(list)) {
                fact = list->data;

                if ((gval = ohm_fact_get(fact, name)) == NULL) {
                    errno = EIO;
                    goto failed;
                }

                if (G_VALUE_TYPE(gval) != G_TYPE_STRING) {
                    errno = EINVAL;
                    goto failed;
                }

                arr->string[i] = strdup(g_value_get_string(gval));
            }
            break;

        default:
            errno = EINVAL;
            return FALSE;

        failed:
            if (arr != NULL)
                free(arr);
            return FALSE;
        }

        var->type = type;
        *(dres_array_t **)pval = arr;
    }
    else {
        if (llen != 1) {
            errno = EINVAL;
            return FALSE;
        }

        if ((gval = ohm_fact_get(list->data, name)) == NULL) {
            errno = EIO;
            return FALSE;
        }

        switch (btyp) {
            
        case VAR_INT:
            if (G_VALUE_TYPE(gval) != G_TYPE_INT) {
                errno = EINVAL;
                return FALSE;
            }

            var->type = VAR_INT;
            *(int *)pval = g_value_get_int(gval);

            break;

        case VAR_STRING:
            if (G_VALUE_TYPE(gval) != G_TYPE_STRING) {
                errno = EINVAL;
                return FALSE;
            }

            var->type = VAR_STRING;
            *(char **)pval = strdup(g_value_get_string(gval));            

            break;

        default:
            errno = EINVAL;
            return FALSE;
        }
    }

    return TRUE;
}


static int get_local_var(dres_local_var_t *var, const char *name,
                         dres_vartype_t type, void *pval)
{
    OhmFact  *fact;
    GValue   *gval;

    if (var->type != type) {
        errno = EMEDIUMTYPE;
        return FALSE;
    }

    if (!(fact = var->fact) || !(gval = ohm_fact_get(fact, name))) {
        errno = EIO;
        return FALSE;
    }

    switch (G_VALUE_TYPE(gval)) {

    case G_TYPE_INT:
        if (type != VAR_INT) {
            errno = EINVAL;
            return FALSE;
        }
        var->type = VAR_INT;
        *(int *)pval = g_value_get_int(gval);
        break;
            
    case G_TYPE_STRING:
        if (type != VAR_STRING) {
            errno = EINVAL;
            return FALSE;
        }
        var->type = VAR_STRING;
        *(char **)pval = strdup(g_value_get_string(gval));
        break;
        
    default:
        errno = EMEDIUMTYPE;
        return FALSE;
    }

    return TRUE;
}


#if TEST
int main(int argc, char **argv)
{
    static char *accessories[] = { "ihf", "earpiece", "microphone", NULL };

    OhmFactStore *fs;
    OhmFact *fact;
    GValue gval;
    dres_store_t *store;
    dres_var_t *var1, *var2;
    int stamp1, stamp2;
    int val1, val1_ret;
    char *val2, *val2_ret;
    dres_array_t *arr_ret;
    char **acc;

    g_type_init();

    val1 = 123;
    val2 = "Hello world";

    /*
     * fact store variables
     */
    printf("Testing fact store variables\n");

    fs = ohm_fact_store_get_fact_store();

    for (acc = accessories; *acc;  acc++) {
        fact = ohm_fact_new("com.nokia.policy.accessories");
        gval = ohm_value_from_string(*acc);
        ohm_fact_set(fact, "device", &gval);
        if (!ohm_fact_store_insert(fs, fact)) {
            printf("failed to add device %s to fact store", *acc);
            return EIO;
        }
    }

    fact = ohm_fact_new("com.nokia.policy.cpu_load");
    gval = ohm_value_from_int(46);
    ohm_fact_set(fact, "percent", &gval);
    if (!ohm_fact_store_insert(fs, fact)) {
        printf("failed to add cpu_load to fact store");
        return EIO;
    }


    if ((store = dres_store_init(STORE_FACT, "com.nokia.policy")) == NULL) {
        printf("dres_store_init() failed: %s\n", strerror(errno));
        return errno;
    }

    if ((var1 = dres_var_init(store, "accessories", &stamp1)) == NULL ||
        (var2 = dres_var_init(store, "cpu_load"   , &stamp2)) == NULL) {
        printf("dres_var_init() failed: %s\n", strerror(errno));
        return errno;
    }

    dres_store_finish(store);

    stamp1 = stamp2 = 0;

    dres_store_update_timestamps(store, 1);

    if (stamp1 || stamp2) {
        printf("invalid timestamp change (1)\n");
        return EINVAL;
    }

    fact = ohm_fact_new("com.nokia.policy.accessories");
    gval = ohm_value_from_string("usb");
    ohm_fact_set(fact, "device", &gval);
    if (!ohm_fact_store_insert(fs, fact)) {
        printf("failed to add device usb to fact store");
        return EIO;
    }

    dres_store_update_timestamps(store, 2);

    if (stamp1 != 2 || stamp2 != 0) {
        printf("invalid timestamp change (2)\n");
        return EINVAL;
    }

    if (!dres_var_get_value(var1, "device", VAR_STRING_ARRAY, &arr_ret) ||
        !dres_var_get_value(var2, "percent", VAR_INT, &val1_ret)) {
        printf("dres_var_get_value() failed: %s\n", strerror(errno));
        return errno;
    }


    /*
     * local variables
     */
    printf("Testing local store variables\n");

    if ((store = dres_store_init(STORE_LOCAL, NULL)) == NULL) {
        printf("dres_store_init() failed: %s\n", strerror(errno));
        return errno;
    }

    if ((var1 = dres_var_init(store, "var1", &stamp1)) == NULL ||
        (var2 = dres_var_init(store, "var2", &stamp2)) == NULL) {
        printf("dres_var_init() failed: %s\n", strerror(errno));
        return errno;
    }

    dres_store_finish(store);

    if (!dres_var_set_value(var1, "value", VAR_INT, &val1) ||
        !dres_var_set_value(var2, "value", VAR_STRING, &val2)) {
        printf("dres_var_set_value() failed: %s\n", strerror(errno));
        return errno;
    }

    if (!dres_var_get_value(var1, "value", VAR_INT, &val1_ret) ||
        !dres_var_get_value(var2, "value", VAR_STRING, &val2_ret)) {
        printf("dres_var_get_value() failed: %s\n", strerror(errno));
        return errno;
    }

    if (val1 != val1_ret || !val2_ret || strcmp(val2, val2_ret)) {
        printf("vlaue mismatch\n");
        return EINVAL;
    }

    dres_var_destroy(var1);
    dres_var_destroy(var2);

    dres_store_destroy(store);

    return 0;
}

#endif

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
