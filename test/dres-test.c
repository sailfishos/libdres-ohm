#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

#include <dres/dres.h>
#include <prolog/ohm-fact.h>

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
static int  test_handler (dres_t *, char *, dres_action_t *, void **);
static int  fail_handler (dres_t *, char *, dres_action_t *, void **);
static void dump_facts   (char *format, ...);


int
main(int argc, char *argv[])
{
    char *rulefile = TEST_RULEFILE;
    int   i;

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
    
    if (dres_register_handler(dres, "test", test_handler) != 0)
        fatal(3, "failed to register DRES test handler");

    if (dres_register_handler(dres, "fail", fail_handler) != 0)
        fatal(3, "failed to register DRES fail handler");

    if (dres_parse_file(dres, rulefile))
        fatal(4, "failed to parse DRES rule file %s", rulefile);

    if (dres_finalize(dres))
        fatal(5, "failed to finalize DRES rules");
    
    dres_dump_targets(dres);
    printf("======================================\n");

    if (argc < 2) {
        dres_update_goal(dres, "all", NULL);
        dump_facts("----------- all -------------\n");
    }
    else {
        for (i = 2; i < argc; i++) {
            dres_update_goal(dres, argv[i], NULL);
            dump_facts("----------- %s -------------\n", argv[i]);
        }
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
    char  value[64], *end;
    int   new_stamp;

    if (action->nargument != 1)
        return EINVAL;
    
    value[0] = '\0';
    dres_name(dres, action->arguments[0], value, sizeof(value));
    
    new_stamp = (int)strtol(value, &end, 10);
    
    if (end && *end)
        return EINVAL;

    stamp = new_stamp;
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
    GSList   *facts = NULL;
    OhmFact  *fact  = NULL;
    GValue   *gval;
    char      name[32], fullname[64], field[64], value[64];
    int       i, status;

    if (action->nargument < 1 || !(action->nargument & 0x1))
        FAIL(EINVAL);

    name[0] = '\0';
    dres_name(dres, action->arguments[0], name, sizeof(name));
    snprintf(fullname, sizeof(fullname), "%s%s", dres_get_prefix(dres), name);

    
    for (facts = ohm_fact_store_get_facts_by_name(store, fullname);
         facts != NULL;
         facts = g_slist_next(facts)) {

        fact = (OhmFact *)facts->data;
    
        snprintf(value, sizeof(value), "%d", stamp++);
        gval = ohm_value_from_string(value);
        ohm_fact_set(fact, "stamp", gval);

        for (i = 1; i < action->nargument; i += 2) {
            field[0] = '\0';
            value[0] = '\0';
        
            dres_name(dres, action->arguments[i]  , field, sizeof(field));
            dres_name(dres, action->arguments[i+1], value, sizeof(value));
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

    OhmFact **facts = NULL;
    OhmFact  *fact  = NULL;
    GValue   *gval;
    char      name[32], fullname[64], field[64], value[64];
    int       i, status;

    if (action->nargument < 1 || !(action->nargument & 0x1))
        FAIL(EINVAL);

    if ((facts = ALLOC_ARR(OhmFact *, 2)) == NULL)
        FAIL(EINVAL);
    
    name[0] = '\0';
    dres_name(dres, action->arguments[0], name, sizeof(name));
    snprintf(fullname, sizeof(fullname), "%s%s", dres_get_prefix(dres), name);

    if ((fact = ohm_fact_new(fullname)) == NULL)
        FAIL(ENOMEM);
    
    gval = ohm_value_from_string(name);
    ohm_fact_set(fact, "name", gval);

    snprintf(value, sizeof(value), "%d", stamp++);
    gval = ohm_value_from_string(value);
    ohm_fact_set(fact, "stamp", gval);

    for (i = 1; i < action->nargument; i += 2) {
        field[0] = '\0';
        value[0] = '\0';

        dres_name(dres, action->arguments[i]  , field, sizeof(field));
        dres_name(dres, action->arguments[i+1], value, sizeof(value));
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

    GSList  *facts = NULL;
    OhmFact *fact  = NULL;
    GValue  *gval;
    char     name[32], fullname[64], field[64], value[64];
    int      i;

    *ret = NULL;

    if (action->nargument < 3 || !(action->nargument & 0x1))
        return EINVAL;
    
    name[0] = '\0';
    dres_name(dres, action->arguments[0], name, sizeof(name));
    snprintf(fullname, sizeof(fullname), "%s%s", dres_get_prefix(dres), name);

    if ((facts = ohm_fact_store_get_facts_by_name(store, fullname)) == NULL)
        return ENOENT;
    
    while (facts) {
        fact = (OhmFact *)facts->data;
        
        for (i = 1; i < action->nargument; i += 2) {
            field[0] = '\0';
            value[0] = '\0';
            dres_name(dres, action->arguments[i], field, sizeof(field));
            dres_name(dres, action->arguments[i+1], value, sizeof(value));
            
            gval = ohm_fact_get(fact, field);
            if (strcmp(g_value_get_string(gval), value)) {
                printf("mismatch: %s:%s, %s != %s\n", fullname, field,
                       g_value_get_string(gval), value);
                return EINVAL;
            }
        }
        
        facts = g_slist_next(facts);
    }

    return 0;
}


/********************
 * dump_handler
 ********************/
static int
dump_handler(dres_t *dres, char *actname, dres_action_t *action, void **ret)
{
    char name[32];

    if (action->nargument != 1)
        return EINVAL;

    name[0] = '\0';
    dres_name(dres, action->arguments[0], name, sizeof(name));
    dump_facts(name);
    
    *ret = NULL;
    return 0;
}


/********************
 * test_handler
 ********************/
static int
test_handler(dres_t *dres, char *actname, dres_action_t *action, void **ret)
{
    if (action->nargument < 1)
        return EINVAL;

    printf("%s()...\n", __FUNCTION__);

    *ret = NULL;
    return 0;
}


/********************
 * fail_handler
 ********************/
static int
fail_handler(dres_t *dres, char *actname, dres_action_t *action, void **ret)
{
    *ret = NULL;
    return EINVAL;
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
