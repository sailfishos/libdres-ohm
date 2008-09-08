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

static int  stamp_handler(void *data, char *name,
                          vm_stack_entry_t *args, int narg,
                          vm_stack_entry_t *rv);
static int  touch_handler(void *data, char *name,
                          vm_stack_entry_t *args, int narg,
                          vm_stack_entry_t *rv);
static int  fact_handler (void *data, char *name,
                          vm_stack_entry_t *args, int narg,
                          vm_stack_entry_t *rv);
static int  check_handler(void *data, char *name,
                          vm_stack_entry_t *args, int narg,
                          vm_stack_entry_t *rv);
static int  dump_handler (void *data, char *name,
                          vm_stack_entry_t *args, int narg,
                          vm_stack_entry_t *rv);
static void dump_facts   (char *format, ...);

static int  register_handlers(dres_t *dres);


int
main(int argc, char *argv[])
{
    char  *rulefile = TEST_RULEFILE;
    char   precomp[256];
    char  *all[]    = { "all", NULL };
    char **goals    = all;
    int    i, compiled = 0;

    if (argc > 1 && !strcmp(argv[1], "-c")) {
        compiled = 1;
        argc--;
        argv++;
    }

    if (argc > 1)
        rulefile = argv[1];
    
    g_type_init();

    if ((store = ohm_fact_store_get_fact_store()) == NULL)
        fatal(1, "failed to initialize OHM fact store");
    
    if ((dres = dres_init(TEST_PREFIX)) == NULL)
        fatal(2, "failed to initialize DRES library");

    register_handlers(dres);
    
    if (dres_parse_file(dres, rulefile))
        fatal(4, "failed to parse DRES rule file %s", rulefile);

    if (dres_finalize(dres))
        fatal(5, "failed to finalize DRES rules");

    dres_dump_targets(dres);
    printf("=========================================\n");
    
#if 0
    snprintf(precomp, sizeof(precomp), "%sc", rulefile);
    unlink(precomp);
    dres_save(dres, precomp);
    /*exit(0);*/
#endif
    
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

#if 0
    dres_exit(dres);

    if ((dres = dres_load(precomp)) == NULL)
        fatal(6, "failed to load precompiled DRES file %s", precomp);
    
    printf("***** Wow, loaded a compiled DRES file. *****\n");
#endif

    return 0;
}


/********************
 * register_handlers
 ********************/
static int
register_handlers(dres_t *dres)
{
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

    return 0;
}


/********************
 * stamp_handler
 ********************/
static int
stamp_handler(void *data, char *name,
              vm_stack_entry_t *args, int narg, vm_stack_entry_t *rv)
{
    if (narg != 1 || args->type != VM_TYPE_INTEGER)
        return EINVAL;
    
    stamp = args->v.i;
    printf("stamp set to %d\n", stamp);
    
    rv->type = VM_TYPE_INTEGER;
    rv->v.i  = 0;
    
    return 0;
}


/********************
 * touch_handler
 ********************/
static int
touch_handler(void *data, char *name,
              vm_stack_entry_t *args, int narg, vm_stack_entry_t *rv)
{
#define FAIL(ec) do { status = (ec); goto fail; } while (0)

    GSList  *facts = NULL;
    OhmFact *fact  = NULL;
    GValue  *gval;
    char    *factname, stampval[32], *field, *value;
    int      status;

    if (narg <= 0 || args->type != VM_TYPE_STRING)
        FAIL(EINVAL);
    
    factname = args->v.s;
    args++;
    narg--;
    
    for (facts = ohm_fact_store_get_facts_by_name(store, factname);
         facts != NULL;
         facts = g_slist_next(facts)) {

        fact = (OhmFact *)facts->data;
    
        snprintf(stampval, sizeof(stampval), "%d", stamp++);
        gval = ohm_value_from_string(stampval);
        ohm_fact_set(fact, "stamp", gval);
        
        for (; narg > 1; narg -= 2, args += 2) {
            if (args[0].type != VM_TYPE_STRING ||
                args[1].type != VM_TYPE_STRING) {
                status = EINVAL;
                goto fail;
            }

            field = args[0].v.s;
            value = args[1].v.s;

            gval = ohm_value_from_string(value);
            ohm_fact_set(fact, field, gval);
        }

        if (narg != 0) {
            status = EINVAL;
            goto fail;
        }
    }

    rv->type = VM_TYPE_INTEGER;
    rv->v.i  = 0;
    return 0;
    
 fail:
    return status;
}


/********************
 * fact_handler
 ********************/
static int
fact_handler(void *data, char *name,
             vm_stack_entry_t *args, int narg, vm_stack_entry_t *rv)
{
#define FAIL(ec) do { status = (ec); goto fail; } while (0)

    OhmFact     **facts = NULL;
    OhmFact      *fact  = NULL;
    GValue       *gval;
    char         *factname, stampval[32], *field, *value;
    vm_global_t  *g;
    int           status;

    if (narg <= 0 || args->type != VM_TYPE_STRING)
        FAIL(EINVAL);
    
    factname = args->v.s;
    args++;
    narg--;
    
    if ((facts = ALLOC_ARR(OhmFact *, 2)) == NULL)
        FAIL(EINVAL);

    if ((fact = ohm_fact_new(factname)) == NULL)
        FAIL(ENOMEM);
    
    gval = ohm_value_from_string(factname);
    ohm_fact_set(fact, "name", gval);

    snprintf(stampval, sizeof(stampval), "%d", stamp++);
    gval = ohm_value_from_string(stampval);
    ohm_fact_set(fact, "stamp", gval);

    for (; narg > 1; narg -= 2, args += 2) {
        if (args[0].type != VM_TYPE_STRING || args[1].type != VM_TYPE_STRING) {
            status = EINVAL;
            goto fail;
        }

        field = args[0].v.s;
        value = args[1].v.s;
        
        gval = ohm_value_from_string(value);
        ohm_fact_set(fact, field, gval);
    }

    if (narg != 0) {
        status = EINVAL;
        goto fail;
    }

    if ((g = vm_global_alloc(1)) == NULL) {
        status = ENOMEM;
        goto fail;
    }
     
    g->facts[0] = fact;
    g->nfact    = 1;
    
    rv->type = VM_TYPE_GLOBAL;
    rv->v.g  = g;
    
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
check_handler(void *data, char *name,
              vm_stack_entry_t *args, int narg, vm_stack_entry_t *rv)
{
#define FAIL(ec) do { status = (ec); goto fail; } while (0)

    GSList  *facts = NULL;
    OhmFact *fact  = NULL;
    GValue  *gval;
    char    *factname, *field, *value;

    if (narg <= 0 || args->type != VM_TYPE_STRING)
        return EINVAL;
    
    factname = args->v.s;
    args++;
    narg--;

    rv->type = VM_TYPE_INTEGER;
    rv->v.i  = 0;

    if ((facts = ohm_fact_store_get_facts_by_name(store, factname)) == NULL)
        return ENOENT;
    
    while (facts) {
        fact = (OhmFact *)facts->data;

        for (; narg > 1; narg -= 2, args += 2) {
            if (args[0].type != VM_TYPE_STRING ||
                args[1].type != VM_TYPE_STRING)
                return EINVAL;
            
            field = args[0].v.s;
            value = args[1].v.s;

            gval = ohm_fact_get(fact, field);
            gval = ohm_value_from_string(value);
            ohm_fact_set(fact, field, gval);

            if (strcmp(g_value_get_string(gval), value)) {
                printf("mismatch: %s:%s, %s != %s\n", factname, field,
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
dump_handler(void *data, char *name,
             vm_stack_entry_t *args, int narg, vm_stack_entry_t *rv)
{
    if (narg != 1 || args->type != VM_TYPE_STRING)
        return EINVAL;
    
    
    dump_facts(args->v.s);
    
    rv->type = VM_TYPE_INTEGER;
    rv->v.i  = 0;
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
