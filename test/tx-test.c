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
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

#include <ohm/ohm-fact.h>

#define TEST_PREFIX   "test"

#define fatal(ec, fmt, args...) do {                \
        printf("fatal error: " fmt "\n", ## args);  \
        exit(ec);                                   \
    } while (0)



static OhmFactStore *store;

static __attribute__((sentinel)) int  fact_add(char *, ...);
static __attribute__((sentinel)) int  fact_set(char *, ...);
static void dump_facts   (char *format, ...);


int
main(int argc, char *argv[])
{
#define TX_START(store)    ohm_fact_store_transaction_push(store)
#define TX_COMMIT(store)   ohm_fact_store_transaction_pop(store, FALSE)
#define TX_ROLLBACK(store) ohm_fact_store_transaction_pop(store, TRUE)
    
    g_type_init();
    
    if ((store = ohm_fact_store_get_fact_store()) == NULL)
        fatal(1, "failed to initialize OHM fact store");
    
    fact_add("fact1", "foo1", "bar1", NULL);
    fact_add("fact2", "foo2", "bar2", NULL);

    dump_facts("initial: ");
    
    TX_START(store);
    TX_START(store);
    TX_START(store);

    fact_set("fact1", "foo1", "foobar1", NULL);
    
    TX_COMMIT(store);
    dump_facts("after 1st commit: ");
    
    fact_set("fact2", "foo2", "foobar2", NULL);

    TX_COMMIT(store);
    
    dump_facts("after 2nd commit: ");
    
    TX_ROLLBACK(store);

    dump_facts("after rollback: ");

    return 0;

    (void)argc;
    (void)argv;
}


/********************
 * fact_add
 ********************/
static int
fact_add(char *name, ...)
{
    va_list  ap;
    char    *field, *value;
    OhmFact *fact;
    GValue  *gval;
    
    if ((fact = ohm_fact_new(name)) == NULL)
        return ENOMEM;
    
    va_start(ap, name);
    while ((field = va_arg(ap, char *)) != NULL &&
           (value = va_arg(ap, char *)) != NULL) {
        gval = ohm_value_from_string(value);
        ohm_fact_set(fact, field, gval);
    }
    va_end(ap);
    
    return(!ohm_fact_store_insert(store, fact) ? EINVAL : 0);
}


/********************
 * fact_set
 ********************/
static int
fact_set(char *name, ...)
{
    va_list  ap;
    GSList  *facts;
    OhmFact *fact;
    GValue   *gval;
    char     *field, *value;

    va_start(ap, name);
    for (facts = ohm_fact_store_get_facts_by_name(store, name);
         facts != NULL;
         facts = g_slist_next(facts)) {
        fact = (OhmFact *)facts->data;

        while ((field = va_arg(ap, char *)) != NULL &&
               (value = va_arg(ap, char *)) != NULL) {
            gval = ohm_value_from_string(value);
            ohm_fact_set(fact, field, gval);
        }
    }
    va_end(ap);
    return 0;
}


/********************
 * dump_facts
 ********************/
static void
dump_facts(char *format, ...)
{
    va_list  ap;
    char    *dump;
    
    va_start(ap, format);
    vprintf(format, ap);
    dump = ohm_fact_store_to_string(store);
    printf("%s", dump);
    g_free(dump);
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
