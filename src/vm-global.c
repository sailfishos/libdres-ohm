#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <dres/mm.h>
#include <dres/vm.h>


/********************
 * vm_global_lookup
 ********************/
int
vm_global_lookup(char *name, vm_global_t **gp)
{
    vm_global_t  *g;
    OhmFactStore *store = ohm_fact_store_get_fact_store();
    GSList       *l;
    int           i, n;
    
    if ((l = ohm_fact_store_get_facts_by_name(store, name)) == NULL ||
        (n = g_slist_length(l)) == 0) {
        *gp = NULL;
        return ENOENT;
    }
    
    if (ALLOC_VAROBJ(g, n, facts) == NULL) {
        *gp = NULL;
        return ENOMEM;
    }
    
    g->nfact = 0;
    for (i = 0; i < n && l; i++, l = g_slist_next(l)) {
        g->facts[i] = (OhmFact *)l->data;
        g_object_ref(g->facts[i]);
        g->nfact++;
    }
    
    *gp = g;
    return 0;
}


/********************
 * vm_global_name
 ********************/
vm_global_t *
vm_global_name(char *name)
{
    vm_global_t *g;
    int          size = sizeof(*g) + strlen(name) + 1;

    if ((g = (vm_global_t *)ALLOC_ARR(char, size)) == NULL)
        return NULL;
    
    g->name = (char *)g->facts;
    strcpy(g->name, name);
    
    return g;
}


/********************
 * vm_global_free
 ********************/
void
vm_global_free(vm_global_t *g)
{
    int i;
    
    if (g != NULL) {
        for (i = 0; i < g->nfact; i++)
            if (g->facts[i])
                g_object_unref(g->facts[i]);
        
        FREE(g);
    }
}


/********************
 * vm_global_print
 ********************/
void
vm_global_print(vm_global_t *g)
{
    int i;

    for (i = 0; i < g->nfact; i++)
        vm_fact_print(g->facts[i]);
}


/*****************************************************************************
 *                            *** fact handling ***                          *
 *****************************************************************************/

/********************
 * vm_fact_lookup
 ********************/
GSList *
vm_fact_lookup(char *name)
{
    return ohm_fact_store_get_facts_by_name(ohm_get_fact_store(), name);
}


/********************
 * vm_fact_reset
 ********************/
void
vm_fact_reset(OhmFact *fact)
{
    GSList *l = (GSList *)ohm_fact_get_fields(fact);
    char   *field;
    GQuark  q;
    
    for ( ; l != NULL; l = g_slist_next(l)) {
        q     = GPOINTER_TO_INT(l->data);
        field = (char *)g_quark_to_string(q);
        ohm_fact_set(fact, field, NULL);
    }
}


/********************
 * vm_fact_dup
 ********************/
OhmFact *
vm_fact_dup(OhmFact *src, char *name)
{
    OhmFact *dst = ohm_fact_new(name);
    GSList  *l   = (GSList *)ohm_fact_get_fields(src);
    char    *field;
    GValue  *value;
    GQuark   q;

    
    if (dst == NULL)
        return NULL;
    
    for ( ; l != NULL; l = g_slist_next(l)){
        q     = GPOINTER_TO_INT(l->data);
        field = (char *)g_quark_to_string(q);
        value = ohm_copy_value(ohm_fact_get(src, field));
        ohm_fact_set(dst, field, value);
    }

    return dst;
}


/********************
 * vm_fact_copy
 ********************/
OhmFact *
vm_fact_copy(OhmFact *dst, OhmFact *src)
{
    GSList *l = (GSList *)ohm_fact_get_fields(src);
    GQuark  q;
    
    vm_fact_reset(dst);

    for ( ; l != NULL; l = g_slist_next(l)) {
        char   *field;
        GValue *value;
        
        q     = GPOINTER_TO_INT(l->data);
        field = (char *)g_quark_to_string(q);
        value = ohm_copy_value(ohm_fact_get(src, field));
        
        if (value == NULL)
            return NULL;
        
        ohm_fact_set(dst, field, value);
    }

    return dst;
}


/********************
 * vm_fact_remove
 ********************/
void
vm_fact_remove(char *name)
{
    OhmFactStore *store = ohm_fact_store_get_fact_store();
    OhmFact      *fact;
    GSList       *l;

    for (l = vm_fact_lookup(name); l != NULL; l = g_slist_next(l)) {
        fact = (OhmFact *)l->data;
        ohm_fact_store_remove(store, fact);
    }
}


/********************
 * vm_fact_set_field
 ********************/
int
vm_fact_set_field(vm_state_t *vm, OhmFact *fact, char *field,
                  int type, vm_value_t *value)
{
    GValue *gval;
    
    switch (type) {
    case VM_TYPE_INTEGER: gval = ohm_value_from_int(value->i);    break;
    case VM_TYPE_DOUBLE:  gval = ohm_value_from_double(value->d); break;
    case VM_TYPE_STRING:  gval = ohm_value_from_string(value->s); break;
    default: VM_EXCEPTION(vm, "invalid type 0x%x for field %s", type, field);
    }

    ohm_fact_set(fact, field, gval);
    return 1;
}


/********************
 * vm_fact_match_field
 ********************/
int
vm_fact_match_field(vm_state_t *vm, OhmFact *fact, char *field,
                    GValue *gval, int type, vm_value_t *value)
{
    int         i;
    double      d;
    const char *s;

    switch (type) {
    case VM_TYPE_INTEGER:
        switch (G_VALUE_TYPE(gval)) {
        case G_TYPE_INT:   i = g_value_get_int(gval);   break;
        case G_TYPE_UINT:  i = g_value_get_uint(gval);  break;
        case G_TYPE_LONG:  i = g_value_get_long(gval);  break;
        case G_TYPE_ULONG: i = g_value_get_ulong(gval); break;
        default: VM_EXCEPTION(vm, "integer type expected for field %s", field);
        }
        return i == value->i;

    case VM_TYPE_DOUBLE:
        switch (G_VALUE_TYPE(gval)) {
        case G_TYPE_DOUBLE: d = g_value_get_double(gval);    break;
        case G_TYPE_FLOAT:  d = 1.0*g_value_get_float(gval); break;
        default: VM_EXCEPTION(vm, "double type expected for field %s", field);
        }
        return d == value->d;

    case VM_TYPE_STRING:
        switch (G_VALUE_TYPE(gval)) {
        case G_TYPE_STRING: s = g_value_get_string(gval); break;
        default: VM_EXCEPTION(vm, "string type expected for field %s", field);
        }
        return !strcmp(s, value->s);

    default:
        VM_EXCEPTION(vm, "unexpected field type 0x%x for filter", type);
    }
    
    return 0;
}


/********************
 * vm_fact_get_field
 ********************/
int
vm_fact_get_field(vm_state_t *vm, OhmFact *fact, char *field, vm_value_t *value)
{
    GValue *gval = ohm_fact_get(fact, field);

    if (gval == NULL)
        return VM_TYPE_UNKNOWN;

    switch (G_VALUE_TYPE(gval)) {
    case G_TYPE_INT:   value->i = g_value_get_int(gval);   goto inttype;
    case G_TYPE_UINT:  value->i = g_value_get_uint(gval);  goto inttype;
    case G_TYPE_LONG:  value->i = g_value_get_long(gval);  goto inttype;
    case G_TYPE_ULONG: value->i = g_value_get_ulong(gval);
    inttype:
        return VM_TYPE_INTEGER;
        
    case G_TYPE_DOUBLE: value->d = g_value_get_double(gval); goto dbltype;
    case G_TYPE_FLOAT:  value->d = 1.0*g_value_get_float(gval);
    dbltype:
        return VM_TYPE_DOUBLE;
        
    case G_TYPE_STRING: value->s = (char *)g_value_get_string(gval);
        return VM_TYPE_STRING;

    default:
        VM_EXCEPTION(vm, "unexpected field type field %s", field);
    }
    
    return VM_TYPE_UNKNOWN;
}


/********************
 * vm_fact_print
 ********************/
void
vm_fact_print(OhmFact *fact)
{
    char *s = ohm_structure_to_string(OHM_STRUCTURE(fact));

    printf("%s", s ? s: "<invalid fact>");
    g_free(s);
}




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


