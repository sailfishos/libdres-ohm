#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <glib.h>
#include <gmodule.h>
#include <dbus/dbus.h>

#include <dres/dres.h>
#include <prolog/prolog.h>

#include <ohm/ohm-plugin.h>
#include <ohm/ohm-plugin-debug.h>
#include <ohm/ohm-plugin-log.h>
#include <ohm/ohm-fact.h>

#include "console.h"
#include "factstore.h"

#define DEFAULT_CONSOLE "127.0.0.1:3000"
#ifndef __PRECOMPILED_RULESET__
#define DEFAULT_RULESET "/usr/share/policy/rules/current/policy.dres"
#else
#define DEFAULT_RULESET "/usr/share/policy/rules/current/policy.dresc"
#endif



/* debug flags */
static int DBG_RESOLVE, DBG_PROLOG, DBG_SIGNAL, DBG_FACTS;

OHM_DEBUG_PLUGIN(resolver,
    OHM_DEBUG_FLAG("resolver", "dependency resolving", &DBG_RESOLVE),
    OHM_DEBUG_FLAG("prolog"  , "prolog handler"      , &DBG_PROLOG),
    OHM_DEBUG_FLAG("signal"  , "decision emission"   , &DBG_SIGNAL),
    OHM_DEBUG_FLAG("facts"   , "fact handling"       , &DBG_FACTS));


/* rule engine methods */
OHM_IMPORTABLE(void, rules_free_result, (void *retval));
OHM_IMPORTABLE(void, rules_dump_result, (void *retval));
OHM_IMPORTABLE(void, rules_prompt     , (void));

OHM_IMPORTABLE(int , rule_find        , (char *name, int arity));
OHM_IMPORTABLE(int , rule_eval        ,
               (int rule, void *retval, void **args, int narg));


static void plugin_exit(OhmPlugin *plugin);

static int  rules_init(void);
static void rules_exit(void);
static int  rule_lookup(const char *name, int arity);

static GHashTable *ruletbl;


static int  resolver_init(void);
static void resolver_exit(void);



/* signaling */
typedef void (*completion_cb_t)(int transid, int success);

OHM_IMPORTABLE(int, signal_changed,(char *signal, int transid,
                                    int factc, char **factv,
                                    completion_cb_t callback,
                                    unsigned long timeout));

OHM_IMPORTABLE(void, completion_cb, (int transid, int success));

static void dump_signal_changed_args(char *signame, int transid, int factc,
                                     char**factv, completion_cb_t callback,
                                     unsigned long timeout);
static int  retval_to_facts(char ***objects, OhmFact **facts, int max);



DRES_ACTION(rule_handler);
DRES_ACTION(signal_handler);

static char   *ruleset;
static dres_t *dres;

typedef struct {
    const char     *name;
    dres_handler_t  handler;
} handler_t;

static handler_t handlers[] = {
    { "prolog"        , rule_handler,   },
    { "rule"          , rule_handler,   },
    { "signal_changed", signal_handler, },
    { NULL, NULL }
};


/*****************************************************************************
 *                       *** initialization & cleanup ***                    *
 *****************************************************************************/

/**
 * plugin_init:
 **/
static void
plugin_init(OhmPlugin *plugin)
{
    const char *console;

    if (!OHM_DEBUG_INIT(resolver))
        OHM_WARNING("resolver plugin failed to initialize debugging");
    
    if ((console = ohm_plugin_get_param(plugin, "console")) == NULL)
        console = DEFAULT_CONSOLE;

    if ((ruleset = ohm_plugin_get_param(plugin, "ruleset")) == NULL)
        ruleset = DEFAULT_RULESET;

    if (resolver_init() != 0 || rules_init() != 0 || factstore_init() != 0 ||
        console_init(console ? console : DEFAULT_CONSOLE) != 0) {
        plugin_exit(plugin);
        exit(1);
    }
    
    OHM_DEBUG(DBG_RESOLVE, "resolver initialized");
    return;
}


/**
 * plugin_exit:
 **/
static void
plugin_exit(OhmPlugin *plugin)
{
    factstore_exit();
    resolver_exit();
    rules_exit();
    console_exit();

    (void)plugin;
}



/********************
 * dres_parse_error
 ********************/
void
dres_parse_error(dres_t *dres, int lineno, const char *msg, const char *token)
{
    g_warning("error: %s, on line %d near input %s\n", msg, lineno, token);
    exit(1);

    (void)dres;
}


/********************
 * resolver_init
 ********************/
static int
resolver_init(void)
{
    handler_t *h;

    OHM_INFO("resolver: using ruleset %s", ruleset);

    /* initialize resolver with our ruleset */
    OHM_DEBUG(DBG_RESOLVE, "Initializing resolver...");
    if ((dres = dres_open(ruleset)) == NULL) {
        OHM_ERROR("failed to to open resolver file \"%s\"", ruleset);
        return EINVAL;
    }
    
    /* register resolver handlers implemented by us */
    OHM_DEBUG(DBG_RESOLVE, "Registering resolver handlers...");
    for (h = handlers; h->name != NULL; h++) {
        /*                              XXX TODO */
        if (dres_register_handler(dres, (char *)h->name, h->handler) != 0) {
            OHM_ERROR("failed to register resolver handler \"%s\"", h->name);
            return EINVAL;
        }
    }
    
    /* finalize/check resolver ruleset */
    OHM_DEBUG(DBG_RESOLVE, "Finalizing resolver ruleset...");
    if (dres_finalize(dres) != 0) {
        OHM_ERROR("failed to finalize resolver ruleset");
        return EINVAL;
    }
    
    return 0;
}


/********************
 * resolver_exit
 ********************/
static void
resolver_exit(void)
{
    if (dres) {
        dres_exit(dres);
        dres = NULL;
    }
}



/********************
 * rules_init
 ********************/
static int
rules_init(void)
{
    ruletbl = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    /* XXX TODO:
     * if we had an appropriate prolog plugin interface for
     * iterating over the discovered predicates we could prefill the
     * rule table here...
     */
    
    return ruletbl == NULL ? ENOMEM : 0;
}


/********************
 * rules_exit
 ********************/
static void
rules_exit(void)
{
    if (ruletbl != NULL) {
        g_hash_table_destroy(ruletbl);
        ruletbl = NULL;
    }
}




/*****************************************************************************
 *                           *** exported methods ***                        *
 *****************************************************************************/

/********************
 * dres/resolve
 ********************/
OHM_EXPORTABLE(int, update_goal, (char *goal, char **locals))
{
    return dres_update_goal(dres, goal, locals);
}


/*****************************************************************************
 *                          *** DRES action handlers ***                     *
 *****************************************************************************/


/********************
 * rule_handler
 ********************/
DRES_ACTION(rule_handler)
{
#define FAIL(ec) do { err = ec; goto fail; } while (0)
#define MAX_FACTS 63
#define MAX_ARGS  (32*2)

    int            rule;
    char          *rule_name;
    char          *argv[MAX_ARGS];
    char        ***retval;
    vm_global_t   *g = NULL;
    int            i, err;

    
    OHM_DEBUG(DBG_RESOLVE, "rule evaluation (prolog handler) entered...");
    
    if (narg < 1 || args[0].type != DRES_TYPE_STRING)
        return EINVAL;

    retval    = NULL;
    rule_name = args[0].v.s;

    if ((rule = rule_lookup(rule_name, narg)) < 0)
        FAIL(ENOENT);
    
    args++;
    narg--;

    for (i = 0; i < narg; i++) {
        switch (args[i].type) {
        case DRES_TYPE_STRING:
            argv[2*i]   = (char *)'s';
            argv[2*i+1] = args[i].v.s;
            break;
        case DRES_TYPE_INTEGER:
            argv[2*i]   = (char *)'i';
            argv[2*i+1] = (char *)args[i].v.i;
            break;
        case DRES_TYPE_DOUBLE:
            argv[2*i]   = (char *)'d';
            argv[2*i+1] = (char *)&args[i].v.d;
            break;
        default:
            FAIL(EINVAL);
        }
    }
    
    if (!rule_eval(rule, &retval, (void **)argv, narg)) {
        rules_dump_result(retval);             /* dump any exceptions */
        FAIL(EINVAL);
    }
    
    OHM_DEBUG(DBG_RESOLVE, "rule engine gave the following results:");
    rules_dump_result(retval);

    if ((g = vm_global_alloc(MAX_FACTS)) == NULL)
        FAIL(ENOMEM);
    
    if ((g->nfact = retval_to_facts(retval, g->facts, MAX_FACTS)) < 0)
        FAIL(EINVAL);

    rules_free_result(retval);

    rv->type = DRES_TYPE_FACTVAR;
    rv->v.g  = g;
    return 0;
    
 fail:
    if (g)
        vm_global_free(g);
    
    if (retval)
        rules_free_result(retval);
    
    return err;

    (void)data;
    (void)name;

#undef MAX_FACTS
}


/********************
 * signal_handler
 ********************/
DRES_ACTION(signal_handler)
{
#define GET_ARG(var, n, f, t, def) do {          \
        if (args[(n)].type != t)                 \
            (var) = args[(n)].v.f;               \
        else                                     \
            return EINVAL;                       \
    } while (0)
#define GET_INTEGER(n, var) GET_ARG(var, (n), i, DRES_TYPE_INTEGER)
#define GET_DOUBLE(n, var)  GET_ARG(var, (n), d, DRES_TYPE_DOUBLE)
    
#define GET_STRING(n, var, dflt) do {                                   \
        if (args[(n)].type == DRES_TYPE_STRING)                         \
            (var) = args[(n)].v.s;                                      \
        else {                                                          \
            if (args[(n)].type == DRES_TYPE_NIL)                        \
                (var) = (dflt);                                         \
            else                                                        \
                return EINVAL;                                          \
        }                                                               \
    } while (0)

#define MAX_FACTS  64
#define MAX_LENGTH 64
#define TIMEOUT    (5 * 1000)
    
    char *signal_name, *cb_name, *txid_name;
    int   txid;
    int   nfact, i;
    char *facts[MAX_FACTS + 1];
    char  buf  [MAX_FACTS * MAX_LENGTH];
    char *p, *e;
    char *signature;
    int   success;
    
    if (narg < 3)
        return EINVAL;
    
    GET_STRING(0, signal_name, "");
    GET_STRING(1, cb_name, "");
    GET_STRING(2, txid_name, "");

    /* XXX TODO:
     * Notes: IMHO this is wrong. This starts processing txid_name as a
     *        fact. Since Ismos signal_changed->send_ipc_signal code ignores
     *        any facts it cannot look up (with an "ERROR: NO FACTS!"
     *        warning) this does not cause major malfunction but it is still
     *        wrong.
     *
     * Rather, this should be:
     *     nfact = narg - 3;
     *     args += 3;
     *     narg -= 3;
     */

    nfact = narg - 2;
    args += 2;
    narg -= 2;
    
    e = NULL;
    txid = strtol(txid_name, &e, 10);
    if (e != NULL && *e)
        return EINVAL;

    OHM_DEBUG(DBG_SIGNAL, "signal='%s', cb='%s' txid='%s'",
              signal_name, cb_name, txid_name);
    

    for (i = 0, p = buf; i < narg; i++) {
        facts[i] = "";
        switch (args[i].type) {
        case DRES_TYPE_FACTVAR: {
            vm_global_t *g    = args[i].v.g;
            const char  *name;
            if (g->nfact < 1)
                break;
            if (!(name = ohm_structure_get_name(OHM_STRUCTURE(g->facts[0]))))
                break;
            p += snprintf(p, MAX_LENGTH, "%s", name) + 1;
            break;
        }
        case DRES_TYPE_STRING:
            facts[i] = p;
            p += snprintf(p, MAX_LENGTH, "%s", args[i].v.s) + 1;
            break;

        default:
        case DRES_TYPE_NIL:
            facts[i] = "";
            break;
        }
        
    }

    facts[nfact] = NULL;
    
    if (cb_name[0] == '\0') {
        dump_signal_changed_args(signal_name, 0, nfact, facts, NULL, TIMEOUT);
        success = signal_changed(signal_name, 0, nfact, facts, NULL, TIMEOUT);
    }
    else {
        signature = (char *)completion_cb_SIGNATURE;

        if (ohm_module_find_method(cb_name,&signature,(void *)&completion_cb)){
            dump_signal_changed_args(signal_name, txid, nfact,facts,
                                     completion_cb, TIMEOUT);
            success = signal_changed(signal_name, txid, nfact,facts,
                                     completion_cb, TIMEOUT);
        }
        else {
            OHM_DEBUG(DBG_SIGNAL, "could not resolve signal.\n");
            success = FALSE;
        }
    }

    rv->type = DRES_TYPE_INTEGER;
    rv->v.i  = 0;

    return success ? 0 : EINVAL;

    (void)name;
    (void)data;

#undef MAX_LENGTH
#undef MAX_FACTS
#undef TIMEOUT
}




static void dump_signal_changed_args(char *signame, int transid, int factc,
                                     char**factv, completion_cb_t callback,
                                     unsigned long timeout)
{
    int i;

    OHM_DEBUG(DBG_SIGNAL, "calling signal_changed(%s, %d,  %d, %p, %p, %lu)",
          signame, transid, factc, factv, callback, timeout);

    for (i = 0; i < factc; i++)
        OHM_DEBUG(DBG_SIGNAL, "   fact[%d]: '%s'", i, factv[i]);
}



/*****************************************************************************
 *                        *** misc. helper routines ***                      *
 *****************************************************************************/


/********************
 * rule_lookup
 ********************/
static int
rule_lookup(const char *name, int arity)
{
    char     key[128], *keyp;
    int      rule;
    gpointer value;
    
    /*
     * XXX TODO: we could avoid the lookup here by allowing an arbitrary
     *           void *data to be attached to DRES actions (or actually
     *           dres action invocations) and store the rule ID there.
     */
    
    snprintf(key, sizeof(key), "%s/%d", name, arity);
    
    if ((rule = (int)g_hash_table_lookup(ruletbl, key)) > 0)
        return rule - 1;
    
    if ((rule = rule_find((char *)name, arity)) < 0)
        return -1;

    value = (gpointer)(rule + 1);
    keyp  = g_strdup(key);

    if (keyp == NULL) {
        OHM_ERROR("failed to insert rule %s/%d into rule hash table",
                  name, arity);
        return -1;
    }

    g_hash_table_insert(ruletbl, keyp, value);
    return rule;
}





#include "console.c"
#include "factstore.c"
 


OHM_PLUGIN_DESCRIPTION("dres",
                       "0.0.0",
                       "krisztian.litkey@nokia.com",
                       OHM_LICENSE_NON_FREE,
                       plugin_init,
                       plugin_exit,
                       NULL);

OHM_PLUGIN_REQUIRES("rule_engine", "console");

OHM_PLUGIN_PROVIDES_METHODS(dres, 1,
    OHM_EXPORT(update_goal, "resolve")
);

OHM_PLUGIN_REQUIRES_METHODS(dres, 12,
    OHM_IMPORT("rule_engine.find"  , rule_find),
    OHM_IMPORT("rule_engine.eval"  , rule_eval),
    OHM_IMPORT("rule_engine.free"  , rules_free_result),
    OHM_IMPORT("rule_engine.dump"  , rules_dump_result),
    OHM_IMPORT("rule_engine.prompt", rules_prompt),

    OHM_IMPORT("console.open"  , console_open),
    OHM_IMPORT("console.close" , console_close),
    OHM_IMPORT("console.write" , console_write),
    OHM_IMPORT("console.printf", console_printf),
    OHM_IMPORT("console.grab"  , console_grab),
    OHM_IMPORT("console.ungrab", console_ungrab),

    OHM_IMPORT("signaling.signal_changed", signal_changed)
);

                            

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
