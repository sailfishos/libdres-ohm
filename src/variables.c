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

typedef struct {
    char              *name;
    char              *value;
} dres_fldsel_t;

typedef struct {
    int                count;
    dres_fldsel_t     *field;
} dres_selector_t;


static void               free_list_elem(gpointer, gpointer);
static int                set_fact_var(dres_fstore_var_t *, dres_selector_t *,
                                       dres_vartype_t, void *);
static int                assign_fact_var(OhmFact **, OhmFact **, int);
static int                set_local_var_field(dres_local_var_t *, const char *,
                                              dres_vartype_t, void *);
static int                set_fact_var_field(dres_fstore_var_t *,
                                             const char *, dres_selector_t *,
                                             dres_vartype_t, void *);
static int                get_local_var_field(dres_local_var_t *, const char *,
                                              dres_vartype_t, void *);
static int                get_fact_var_field(dres_fstore_var_t *,
                                             const char *, dres_selector_t *,
                                             dres_vartype_t, void *);
static dres_selector_t   *parse_selector(char *);
static void               free_selector(dres_selector_t *);
static int                is_matching(OhmFact *, dres_selector_t *);
static int                is_selector_field(char *, dres_selector_t *);



dres_store_t *dres_store_init(dres_storetype_t type, char *prefix)
{
    dres_store_t     *store;
    OhmFactStore     *fs;
    OhmFactStoreView *view;
    GHashTable       *htbl;

    if (prefix == NULL)
        prefix = "";
    
    if ((store = (dres_store_t *)malloc(sizeof(*store))) == NULL)
        goto failed;            /* errno = ENOMEM; */

    memset(store, 0, sizeof(*store));

    htbl = g_hash_table_new_full(g_str_hash, g_str_equal, free, NULL);
    
    if (!(store->any.htbl = htbl) || !dres_store_set_prefix(store, prefix)) {
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

int dres_store_set_prefix(dres_store_t *store, char *prefix)
{
    char buf[128];

    if (store->any.refcnt > 1) {
        errno = EBUSY;
        return FALSE;
    }
    
    if (store->any.prefix != NULL)
        free(store->any.prefix);

    if (prefix == NULL)
        prefix = "";
    else {
        snprintf(buf, sizeof(buf), "%s.", prefix);
        prefix = buf;
    }

    if ((store->any.prefix = strdup(prefix)) == NULL) {
        errno = ENOMEM;
        return FALSE;
    }

    return TRUE;
}

char *dres_store_get_prefix(dres_store_t *store)
{
    return store->any.prefix;
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

int dres_var_create(dres_store_t *store, char *name, void *pval)
{
    OhmFact  *src = (OhmFact *)pval;
    OhmFact  *dst;

    if (!store || !name || !pval) {
        errno = EINVAL;
        return FALSE;
    }

    if (store->type != STORE_FACT) {
        errno = ENOSYS;
        return FALSE;
    }

    if ((dst = ohm_fact_new(name)) == NULL) {
        errno = EIO;
        return FALSE;
    }

    if (!assign_fact_var(&dst, &src, 1)) {
        g_object_unref(dst);
        return FALSE;
    }

    return TRUE;
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

int dres_var_set(dres_var_t *var, char *selector,
                 dres_vartype_t type, void *pval)
{
    dres_selector_t *select = parse_selector(selector);
    int              retval = FALSE;

    if (!var || !pval)
        errno = EINVAL;
    else {
        switch (var->any.store->type) {

        case STORE_LOCAL:
            break;

        case STORE_FACT:
            retval = set_fact_var(&var->fact, select, type, pval);
            break;

        default:
            errno = ENOSYS;
            break;
        }
    }

    free_selector(select);

    return retval;
}

int dres_var_set_field(dres_var_t *var, const char *name, char *selector,
                       dres_vartype_t type, void *pval)
{
    dres_selector_t *select;
    int              retval;

    if (!var || !name || !pval) {
        errno = EINVAL;
        return FALSE;
    }

    if (!var->any.store) {
        errno = ENOSYS;
        return FALSE;
    }

    switch (var->any.store->type) {

    case STORE_LOCAL:
        retval = set_local_var_field(&var->local, name, type, pval);
        break;

    case STORE_FACT:
        select = parse_selector(selector);
        retval = set_fact_var_field(&var->fact, name, select, type, pval);
        free_selector(select);
        break;

    default:
        errno = ENOSYS;
        return FALSE;
    }

    return retval;
}

int dres_var_get_field(dres_var_t *var, const char *name, char *selector,
                       dres_vartype_t type, void *pval)
{
    dres_store_t    *store;
    dres_selector_t *select;
    int              retval = FALSE;

    if (!var || !name || !pval)
        errno = EINVAL;
    else if ((store = var->any.store) == NULL)
        errno = ENOSYS;
    else {
        switch (store->type) {

        case STORE_LOCAL:
            retval = get_local_var_field(&var->local, name, type, pval);
            break;
            
        case STORE_FACT:
            select = parse_selector(selector);
            retval = get_fact_var_field(&var->fact, name, select, type, pval);
            free_selector(select);
            break;
            
        default:                    /* we should not really get ever here*/
            errno = EINVAL;
            break;
        }
    }

    return retval;
}

int dres_var_get_field_names(dres_var_t *var, char **names, int nname)
{
    dres_fact_store_t *store;
    GSList            *facts;
    OhmFact           *fact;

    if (!var || !names || nname < 0)
        return -1;

    if ((store = &var->any.store->fact) == NULL) {
        errno = EIO;
        return FALSE;
    }

    switch (var->any.store->type) {

    case STORE_LOCAL:
        if (!(fact = var->local.fact))
            return -1;

        break;

    case STORE_FACT:  
        facts = ohm_fact_store_get_facts_by_name(store->fs, var->fact.name);

        if (!facts || g_slist_length(facts) < 1 || !(fact = facts->data))
            return -1;

        break;
        
    default:
        return -1;
    }

    return get_fields(fact, names, nname);
}


void *dres_fact_create(char *name, char *descr)
{
    GValue          gval;
    OhmFact        *fact;
    char            buf[1024];
    char           *p, *q, *e;
    char            c;
    char           *str;
    char           *field;
    dres_vartype_t  type;
    char           *value;
    int             has_typedef;
    long int        ival;
    int             i;

    if (!name || !descr) {
        errno = EINVAL;
        return NULL;
    }

    if ((fact = ohm_fact_new(name)) == NULL) {
        DEBUG("ohm_fact_new() failed");
        errno = EIO;
        return NULL;
    }

    /*
     * copy the descriptor to buf
     * withuout whitespaces NL's etc
     */
    for (p = descr, e = (q = buf) + sizeof(buf)-1;  (c = *p) && q < e;   p++) {
        if (c > 0x20 && c < 0x7f)
            *q++ = c;
    }
    *q = '\0';


    /*
     * parse the fields
     */
    for (i = 0, str = buf; (field = strtok(str, ",")) != NULL; str = NULL) {
        if ((p = strchr(field, ':')) != NULL) {
            *p++  = '\0';
            value = p;
        }
        else {
            DEBUG("Invalid fact descriptor: missing ':' in '%s'", descr);
            errno = EINVAL;
            goto failed;
        }

        if (!strncmp(value, "string(", 7)) {
            value += 7;
            type = VAR_STRING;
            has_typedef = TRUE;
        }
        else if (!strncmp(value, "int(", 4)) {
            value += 4;
            type = VAR_INT;
            has_typedef = TRUE;
        }
        else {
            type = VAR_STRING;
            has_typedef = FALSE;
        }

        if (has_typedef && *(q = value + strlen(value) - 1) != ')') {
            DEBUG("Invalid fact descriptor: missing ')' in '%s'", descr);
            errno = EINVAL;
            goto failed;
        }
        *q = '\0';

        switch (type) {
        case VAR_STRING:
            gval = ohm_value_from_string(value);
            break;
        case VAR_INT:
            ival = strtol(value, &e, 10);
            gval = ohm_value_from_int(ival); 
            if (*e != '\0') {
                DEBUG("Invalid fact descriptor: invalid integer '%s'", value);
                errno = EINVAL;
                goto failed;
            }
            break;
        default:
            errno = EINVAL;
            goto failed;
        }

        ohm_fact_set(fact, field, &gval);
    }

    return (void *)fact;

 failed:
    dres_fact_destroy(fact);
    return NULL;
}

void dres_fact_destroy(void *vfact)
{
    OhmFact *fact = (OhmFact *)vfact;

    if (fact != NULL) {
    }
}


static void free_list_elem(gpointer data, gpointer user_data)
{
    g_object_unref(data);
}


static int set_fact_var(dres_fstore_var_t *var, dres_selector_t *selector,
                        dres_vartype_t type, void *pval)
{
#define FACT_DIM  1024

    dres_fact_store_t *store;
    GSList            *list;
    dres_vartype_t     btyp;
    dres_array_t       single;
    dres_array_t      *arr;
    int                llen;
    OhmFact           *fact;
    OhmFact           *facts[FACT_DIM];
    int                flen;
    int                i, j;

    if (!var || !pval) {
        errno = EINVAL;
        return FALSE;
    }

    if ((store = &var->store->fact) == NULL) {
        errno = EIO;
        return FALSE;
    }

    list   = ohm_fact_store_get_facts_by_name(store->fs, var->name);
    llen   = list ? g_slist_length(list) : 0;
    btyp   = VAR_BASE_TYPE(type);

    if (VAR_IS_ARRAY(type)) {
        arr = (dres_array_t *)pval;

        for (i = flen = 0;    list != NULL;   i++, list = g_slist_next(list)) {
            fact = (OhmFact *)list->data;

            if (!selector || is_matching(fact, selector))
                facts[flen++] = fact;

            if (flen >= FACT_DIM) {
                errno = EIO;
                return FALSE;
            }
        }

        if (flen != arr->len) {
            errno = EINVAL;
            return FALSE;
        }
    }
    else {
        single.len  = 1;
        single.fact[0] = pval;

        arr = &single;

        if (!selector) {
            flen = llen;
            facts[0] = (OhmFact *)list->data;
        }
        else {       
            for (flen = 0;   list != NULL;   list = g_slist_next(list)) {
                if (is_matching(list->data, selector))
                    facts[flen++] = (OhmFact *)list->data;

                if (flen >= FACT_DIM) {
                    errno = EIO;
                    return FALSE;
                }
            }
        }

        if (flen != 1) {
            errno = EINVAL;
            return FALSE;
        }
    }

    return assign_fact_var(facts, (OhmFact **)arr->fact, flen);

#undef FACT_DIM
}


static int assign_fact_var(OhmFact **dst, OhmFact **src, int count)
{
#define FIELD_DIM 256
    static GValue noval = {G_TYPE_INVALID, };

    OhmFact *dfact;
    OhmFact *sfact;
    GValue  *gval;
    char    *names[FIELD_DIM];
    int      nname;
    int      i, j;

    for (i = 0;   i < count;  i++) {
        dfact = dst[i];
        sfact = src[i];
        nname = get_fields(dfact, names, FIELD_DIM);
        
        for (j = 0;  j < nname;  j++)
            ohm_fact_set(dfact, names[j], &noval);

        nname = get_fields(sfact, names, FIELD_DIM);

        for (j = 0;  j < nname;  j++) {
            if ((gval = ohm_fact_get(sfact, names[j])) == NULL) {
                errno = EIO;
                return FALSE;
            }

            ohm_fact_set(dfact, names[j], gval);
        }
    }

    return TRUE;

#undef FIELD_DIM
}

static int set_fact_var_field(dres_fstore_var_t *var,
                              const char *name, dres_selector_t *selector,
                              dres_vartype_t type, void *pval)
{

}


static int set_local_var_field(dres_local_var_t *var, const char *name,
                               dres_vartype_t type, void *pval)
{
    static int   empty_int = 0;
    static char *empty_str = "";

    GValue gval;

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

    ohm_fact_set(var->fact, name, &gval);

    return TRUE;
}

static int get_fact_var_field(dres_fstore_var_t *var,
                              const char *name, dres_selector_t *selector,
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

            arr->string[i] = NULL;

            break;

        case VAR_FACT:
            if ((arr = malloc(sizeof(*arr) + (llen * sizeof(void *)))) == NULL)
                return FALSE;
            
            arr->len = llen;
            
            for (i = 0;    list != NULL;   i++, list = g_slist_next(list)) {
                fact = list->data;
                arr->fact[i] = g_object_ref(fact);
            }
            
            arr->fact[i] = NULL;

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
        if (selector == NULL) {
            if (llen != 1) {
                errno = EINVAL;
                return FALSE;
            }
            fact = (OhmFact *)list->data;
        }
        else {
            while (list != NULL) {
                fact = (OhmFact *)list->data;
                if (is_matching(fact, selector))
                    break;
                list = g_slist_next(list);
            }
            if (list == NULL) {
                errno = EINVAL;
                return FALSE;
            }
        }
            

        if (btyp != VAR_FACT) {
            if ((gval = ohm_fact_get(fact, name)) == NULL) {
                errno = EIO;
                return FALSE;
            }
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

        case VAR_FACT:
            var->type = VAR_FACT;
            *(OhmFact **)pval = g_object_ref(fact);
            break;

        default:
            errno = EINVAL;
            return FALSE;
        }
    }

    return TRUE;
}


static int get_local_var_field(dres_local_var_t *var, const char *name,
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


/* static */
int get_fields(OhmFact *fact, char **names, int nname)
{
    GList             *field;
    GQuark             qk;
    int                i;

    if (!fact || !names || nname < 0)
        return -1;

    if ((field = ohm_fact_get_fields(fact)) == NULL)
        return -1;

    for (i = 0;   field != NULL;   i++, field = field->next) {
        if (i < nname) {
            qk = (GQuark)GPOINTER_TO_INT(field->data);
            names[i] = (char *)g_quark_to_string(qk);
        }
    }

    return i < nname ? i : -1;
}


static dres_selector_t *parse_selector(char *descr)
{
    dres_selector_t *selector;
    dres_fldsel_t   *field;
    char            *p, *q, c;
    char            *str;
    char            *name;
    char            *value;
    char             buf[1024];
    int              i;

    
    if (descr == NULL) {
        errno = 0;
        return NULL;
    }

    for (p = descr, q = buf;  (c = *p) != '\0';   p++) {
        if (c > 0x20 && c < 0x7f)
            *q++ = c;
    }

    if ((selector = malloc(sizeof(*selector))) == NULL)
        return NULL;
    memset(selector, 0, sizeof(*selector));

    for (i = 0, str = buf;   (name = strtok(str, ",")) != NULL;   str = NULL) {
        if ((p = strchr(name, '=')) == NULL)
            DEBUG("Invalid selctor: '%s'", descr);
        else {
            *p++ = '\0';
            value = p;

            selector->count++;
            selector->field = realloc(selector->field,
                                      sizeof(dres_fldsel_t) * selector->count);

            if (selector->field == NULL)
                return NULL; /* maybe better not to attempt to free anything */
            
            field = selector->field + selector->count - 1;

            field->name  = strdup(name);
            field->value = strdup(value);
        }
    }
   
    return selector;
}

static void free_selector(dres_selector_t *selector)
{
    int i;

    if (selector != NULL) {
        for (i = 0;   i < selector->count;   i++) {
            free(selector->field[i].name);
            free(selector->field[i].value);
        }

        free(selector);
    }
}

static int is_matching(OhmFact *fact, dres_selector_t *selector)
{
    dres_fldsel_t *fldsel;
    GValue        *gval;
    long int       ival;
    char          *e;
    int            i;
    int            match;
  
    if (fact == NULL || selector == NULL)
        match = FALSE;
    else {
        match = TRUE;

        for (i = 0;    match && i < selector->count;    i++) {
            fldsel = selector->field + i;

            if ((gval = ohm_fact_get(fact, fldsel->name)) == NULL)
                match = FALSE;
            else {
                switch (G_VALUE_TYPE(gval)) {
                    
                case G_TYPE_STRING:
                    match = !strcmp(g_value_get_string(gval), fldsel->value);
                    break;
                    
                case G_TYPE_INT:
                    ival  = strtol(fldsel->value, &e, 10);
                    match = (*e == '\0' && g_value_get_int(gval) == ival);
                    break;

                default:
                    match = FALSE;
                    break;
                }
            }
        } /* for */
    }

    return match;
}

static int is_selector_field(char *name, dres_selector_t *selector)
{
    dres_fldsel_t *fldsel;
    int            i;
  
    if (name == NULL || selector == NULL)
        return FALSE;

    for (i = 0;    i < selector->count;    i++) {
        fldsel = selector->field + i;

        if (!strcmp(name, fldsel->name))
            return TRUE;
    }

    return FALSE;
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

    if (!dres_var_get_field(var1, "device",NULL, VAR_STRING_ARRAY, &arr_ret) ||
        !dres_var_get_field(var2, "percent",NULL, VAR_INT, &val1_ret)) {
        printf("dres_var_get_field() failed: %s\n", strerror(errno));
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

    if (!dres_var_set_field(var1, "value",NULL, VAR_INT, &val1) ||
        !dres_var_set_field(var2, "value",NULL, VAR_STRING, &val2)) {
        printf("dres_var_set_field() failed: %s\n", strerror(errno));
        return errno;
    }

    if (!dres_var_get_field(var1, "value",NULL, VAR_INT, &val1_ret) ||
        !dres_var_get_field(var2, "value",NULL, VAR_STRING, &val2_ret)) {
        printf("dres_var_get_field() failed: %s\n", strerror(errno));
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
