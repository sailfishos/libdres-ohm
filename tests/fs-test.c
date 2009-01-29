#include <stdio.h>
#include <stdlib.h>

#include <ohm/ohm-fact.h>

#define FACT_NAME  "test"
#define FACT_FIELD "field"
#define FACT_VALUE "value"

#define fatal(ec, fmt, args...) do {                            \
        fprintf(stderr, "FATAL ERROR: "fmt"\n" , ## args);      \
        exit(ec);                                               \
    } while (0)


static OhmFactStore     *store;
static OhmFact          *fact;
static OhmFactStoreView *view;


void startup(void)
{
    OhmPattern *pattern;

    g_type_init();
    
   if ((store = ohm_get_fact_store()) == NULL)
        fatal(1, "Failed to create test factstore.");

    if ((fact = ohm_fact_new(FACT_NAME)) == NULL)
        fatal(1, "Failed to create test fact.");

    ohm_fact_set(fact, FACT_FIELD, ohm_value_from_string(FACT_VALUE));
    
    if (!ohm_fact_store_insert(store, fact))
        fatal(1, "Failed to insert test fact into factstore.");

    if ((view = ohm_fact_store_new_view(store, NULL)) == NULL)
        fatal(1, "Failed to create test view.");

    if ((pattern = ohm_pattern_new(FACT_NAME)) == NULL)
        fatal(1, "Failed to create test pattern.");

    ohm_fact_store_view_add(view, OHM_STRUCTURE(pattern));
    g_object_unref(pattern);
}


void shutdown(void)
{
    OhmPattern *pattern;
    GSList     *p, *n;

    if (fact && store) {
        ohm_fact_store_remove(store, fact);
        g_object_unref(fact);
        fact = NULL;
    }

    if (view) {
        for (p = view->patterns; p != NULL; p = n) {
            n = p->next;
            pattern = p->data;
            if (!OHM_IS_PATTERN(pattern))
                fatal(1, "Non-pattern object in view.");

            ohm_fact_store_view_remove(view, OHM_STRUCTURE(pattern));
            g_object_unref(pattern);
        }

        g_object_unref(view);
        view = NULL;
    }
    
    if (store != NULL) {
        g_object_unref(store);
        store = NULL;
    }
}

void
vm_fact_reset(OhmFact *fact)
{
    GSList *l = (GSList *)ohm_fact_get_fields(fact);
    char   *field;
    GQuark  quark;
    
    for ( ; l != NULL; l = g_slist_next(l)) {
        quark = GPOINTER_TO_INT(l->data);
        field = (char *)g_quark_to_string(quark);
        if (field != NULL)
            ohm_fact_set(fact, field, NULL);
        else
            fprintf(stderr, "*** field for quark 0x%x is NULL\n", quark);
    }
}

void test_cycle(int i)
{
    static int count = 0;
    char       value[64];

    snprintf(value, sizeof(value) - 1, "%s-#%d", FACT_VALUE, count++);
    
    ohm_fact_store_transaction_push(store);

    printf("test cycle #%d...\n", count);
    vm_fact_reset(fact);
    ohm_fact_set(fact, FACT_FIELD, ohm_value_from_string(value));

    ohm_fact_store_transaction_pop(store, i & 0x1);
}


int main(int argc, char *argv[])
{
    int loops, i;

    if (argc == 2) {
        loops = (int)strtol(argv[1], NULL, 10);
        printf("Running %d test iterations...\n", loops);
    }
    else
        loops = 3;

    startup();

    for (i = 0; i < loops; i++)
        test_cycle(i);
    
    shutdown();
    
    exit(0);
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
