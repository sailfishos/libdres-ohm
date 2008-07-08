#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

#include <dres/dres.h>
#include <ohm/ohm-fact.h>

#define TEST_PREFIX   "test"
#define TEST_RULEFILE "./test.dres"


#define fatal(ec, fmt, args...) do {                \
        printf("fatal error: " fmt "\n", ## args);  \
        exit(ec);                                   \
    } while (0)



static OhmFactStore *store;
static dres_t       *dres;

static int           stamp = 0;

static int  stamp_handler(dres_t *, char *, dres_action_t *, void **);
static int  touch_handler(dres_t *, char *, dres_action_t *, void **);
static int  fact_handler (dres_t *, char *, dres_action_t *, void **);
static int  check_handler(dres_t *, char *, dres_action_t *, void **);
static int  dump_handler (dres_t *, char *, dres_action_t *, void **);
static void dump_facts   (char *format, ...);


int
main(int argc, char *argv[])
{
    char  *rulefile = TEST_RULEFILE;
    char  *all[]    = { "all", NULL };
    char **goals    = all;
    int    i;

    if (argc > 1)
        rulefile = argv[1];

    g_type_init();
    
    if ((store = ohm_fact_store_get_fact_store()) == NULL)
        fatal(1, "failed to initialize OHM fact store");
    
    if ((dres = dres_init(TEST_PREFIX)) == NULL)
        fatal(2, "failed to initialize DRES library");

    if (dres_register_handler(dres, "stamp", stamp_handler) != 0)
        fatal(3, "failed to register DRES stamp handler");
    
    if (dres_register_handler(dres, "touch", touch_handler) != 0)
        fatal(3, "failed to register DRES touch handler");

    if (dres_register_handler(dres, "fact", fact_handler) != 0)
        fatal(3, "failed to register DRES fact handler");
    
    if (dres_register_handler(dres, "check", check_handler) != 0)
        fatal(3, "failed to register DRES check handler");
    
    if (dres_register_handler(dres, "dump", dump_handler) != 0)
        fatal(3, "failed to register DRES dump handler");
    
    if (dres_parse_file(dres, rulefile))
        fatal(4, "failed to parse DRES rule file %s", rulefile);

    if (dres_finalize(dres))
        fatal(5, "failed to finalize DRES rules");

    dres_dump_targets(dres);
    
    /*exit(0);*/


    if (argc > 2)
        goals = argv + 2;

    for (i = 0; goals[i]; i++) {
        if (!strcmp(goals[i], "dump")) {
            dres_dump_targets(dres);
            printf("======================================\n");
            continue;
        }

        dres_update_goal(dres, goals[i], NULL);
        dump_facts("----------- %s -------------\n", goals[i]);
    }
                
    
#if 0
    dres_update_goal(dres, "test2", NULL);
    dump_facts("----------- test2 -------------\n");

    dres_update_goal(dres, "test3", NULL);
    dump_facts("----------- test3 -------------\n");

    dres_update_goal(dres, "test4", NULL);
    dump_facts("----------- test4 -------------\n");
#endif

    dres_exit(dres);
    
    return 0;
}


/********************
 * stamp_handler
 ********************/
static int
stamp_handler(dres_t *dres, char *actname, dres_action_t *action, void **ret)
{
    dres_call_t *call = action->call;
    dres_arg_t  *args = call->args;

    if (args == NULL || args->value.type != DRES_TYPE_INTEGER)
        return EINVAL;
    
    stamp = args->value.v.i;
    printf("stamp set to %d\n", stamp);

    *ret = NULL;
    return 0;
}


/********************
 * touch_handler
 ********************/
static int
touch_handler(dres_t *dres, char *actname, dres_action_t *action, void **ret)
{
#define FAIL(ec) do { status = (ec); goto fail; } while (0)

    dres_call_t *call = action->call;
    dres_arg_t  *args = call->args, *a, *v;
    GSList      *facts = NULL;
    OhmFact     *fact  = NULL;
    GValue      *gval;
    char        *name, fullname[64], stampval[32], *field, *value;
    int          status;

    if (args == NULL || args->value.type != DRES_TYPE_STRING)
        FAIL(EINVAL);
    
    name = args->value.v.s;
    args = args->next;

    snprintf(fullname, sizeof(fullname), "%s%s", dres_get_prefix(dres), name);
    
    for (facts = ohm_fact_store_get_facts_by_name(store, fullname);
         facts != NULL;
         facts = g_slist_next(facts)) {

        fact = (OhmFact *)facts->data;
    
        snprintf(stampval, sizeof(stampval), "%d", stamp++);
        gval = ohm_value_from_string(stampval);
        ohm_fact_set(fact, "stamp", gval);
        
        for (a = args; a != NULL; a = v->next) {
            if (a->value.type != DRES_TYPE_STRING ||
                (v = a->next) == NULL || v->value.type != DRES_TYPE_STRING) {
                status = EINVAL;
                goto fail;
            }

            field = a->value.v.s;
            value = v->value.v.s;

            gval = ohm_value_from_string(value);
            ohm_fact_set(fact, field, gval);
        }
    }

    *ret = NULL;
    return 0;
    
 fail:
    return status;
}


/********************
 * fact_handler
 ********************/
static int
fact_handler(dres_t *dres, char *actname, dres_action_t *action, void **ret)
{
#define FAIL(ec) do { status = (ec); goto fail; } while (0)

    dres_call_t *call = action->call;
    dres_arg_t  *args = call->args, *a, *v;

    OhmFact **facts = NULL;
    OhmFact  *fact  = NULL;
    GValue   *gval;
    char     *name, fullname[64], stampval[32], *field, *value;
    int       status;

    if (args == NULL || args->value.type != DRES_TYPE_STRING)
        FAIL(EINVAL);
    
    name = args->value.v.s;
    args = args->next;

    if ((facts = ALLOC_ARR(OhmFact *, 2)) == NULL)
        FAIL(EINVAL);
    
    snprintf(fullname, sizeof(fullname), "%s%s", dres_get_prefix(dres), name);

    if ((fact = ohm_fact_new(fullname)) == NULL)
        FAIL(ENOMEM);
    
    gval = ohm_value_from_string(name);
    ohm_fact_set(fact, "name", gval);

    snprintf(stampval, sizeof(stampval), "%d", stamp++);
    gval = ohm_value_from_string(stampval);
    ohm_fact_set(fact, "stamp", gval);

    for (a = args; a != NULL; a = v->next) {
        if (a->value.type != DRES_TYPE_STRING ||
            (v = a->next) == NULL || v->value.type != DRES_TYPE_STRING) {
            status = EINVAL;
            goto fail;
        }

        field = a->value.v.s;
        value = v->value.v.s;
        
        gval = ohm_value_from_string(value);
        ohm_fact_set(fact, field, gval);
    }

    facts[0] = fact;
    facts[1] = NULL;
    *ret     = facts;
    
    return 0;
    
 fail:
    if (fact)
        g_object_unref(fact);
    if (facts)
        FREE(facts);

    return EINVAL;
}


/********************
 * check_handler
 ********************/
static int
check_handler(dres_t *dres, char *actname, dres_action_t *action, void **ret)
{
#define FAIL(ec) do { status = (ec); goto fail; } while (0)

    dres_call_t *call = action->call;
    dres_arg_t  *args = call->args, *a, *v;
    
    GSList  *facts = NULL;
    OhmFact *fact  = NULL;
    GValue  *gval;
    char    *name, fullname[64], *field, *value;

    if (args == NULL || args->value.type != DRES_TYPE_STRING)
        return EINVAL;
    
    name = args->value.v.s;
    args = args->next;

    *ret = NULL;

    snprintf(fullname, sizeof(fullname), "%s%s", dres_get_prefix(dres), name);

    if ((facts = ohm_fact_store_get_facts_by_name(store, fullname)) == NULL)
        return ENOENT;
    
    while (facts) {
        fact = (OhmFact *)facts->data;

        for (a = args; a != NULL; a = v->next) {
            if (a->value.type != DRES_TYPE_STRING ||
                (v = a->next) == NULL || v->value.type != DRES_TYPE_STRING) {
                return EINVAL;
            }

            field = a->value.v.s;
            value = v->value.v.s;

            gval = ohm_fact_get(fact, field);
            gval = ohm_value_from_string(value);
            ohm_fact_set(fact, field, gval);

            if (strcmp(g_value_get_string(gval), value)) {
                printf("mismatch: %s:%s, %s != %s\n", fullname, field,
                       g_value_get_string(gval), value);
                return EINVAL;
            }
        }
        
        facts = g_slist_next(facts);
    }

    return 0;
#undef FAIL
}


/********************
 * dump_handler
 ********************/
static int
dump_handler(dres_t *dres, char *actname, dres_action_t *action, void **ret)
{
    dres_call_t *call = action->call;
    dres_arg_t  *args = call->args;
    char        *name;

    if (args == NULL || args->value.type != DRES_TYPE_STRING)
        return EINVAL;
    
    name = args->value.v.s;
    dump_facts(name);
    
    *ret = NULL;
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



/********************
 * dres_parse_error
 ********************/
void
dres_parse_error(dres_t *dres, int lineno, const char *msg, const char *token)
{
    g_warning("error: %s, on line %d near input %s\n", msg, lineno, token);
    exit(1);
}




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
