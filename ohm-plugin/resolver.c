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
#include <ohm/ohm-fact.h>


#include "console.h"
#include "factstore.h"


static int DBG_RESOLVE, DBG_PROLOG, DBG_SIGNAL, DBG_FACTS;

OHM_DEBUG_PLUGIN(resolver,
    OHM_DEBUG_FLAG("resolver", "dependency resolving", &DBG_RESOLVE),
    OHM_DEBUG_FLAG("prolog"  , "prolog handler"      , &DBG_PROLOG),
    OHM_DEBUG_FLAG("signal"  , "decision emission"   , &DBG_SIGNAL),
    OHM_DEBUG_FLAG("facts"   , "fact handling"       , &DBG_FACTS));




typedef void (*completion_cb_t)(int transid, int success);




/* rule engine methods */
OHM_IMPORTABLE(int , prolog_setup , (char **extensions, char **files));
OHM_IMPORTABLE(void, prolog_free  , (void *retval));
OHM_IMPORTABLE(void, prolog_dump  , (void *retval));
OHM_IMPORTABLE(void, prolog_prompt, (void));

OHM_IMPORTABLE(prolog_predicate_t *, prolog_lookup ,
               (char *name, int arity));
OHM_IMPORTABLE(int                 , prolog_invoke ,
               (prolog_predicate_t *pred, void *retval, ...));
OHM_IMPORTABLE(int                 , prolog_vinvoke,
               (prolog_predicate_t *pred, void *retval, va_list ap));
OHM_IMPORTABLE(int                 , prolog_ainvoke,
               (prolog_predicate_t *pred, void *retval,void **args, int narg));

OHM_IMPORTABLE(int, signal_changed,(char *signal, int transid,
                                    int factc, char **factv,
                                    completion_cb_t callback,
                                    unsigned long timeout));

OHM_IMPORTABLE(void, completion_cb, (int transid, int success));

DRES_ACTION(prolog_handler);
DRES_ACTION(signal_handler);

static void dump_signal_changed_args(char *signame, int transid, int factc,
                                     char**factv, completion_cb_t callback,
                                     unsigned long timeout);
static int  retval_to_facts(char ***objects, OhmFact **facts, int max);



static dres_t *dres;


/*****************************************************************************
 *                       *** initialization & cleanup ***                    *
 *****************************************************************************/

/**
 * plugin_init:
 **/
static void
plugin_init(OhmPlugin *plugin)
{
#define RULESET "current"

#define DRES_RULE_PATH "/usr/share/policy/rules/"RULESET"/policy.dres"
#define DRES_PLC_PATH  "/usr/share/policy/rules/"RULESET"/policy.plc"
#define PROLOG_SYSDIR  "/usr/lib/prolog/"
#define PROLOG_RULEDIR "/usr/share/policy/rules/"RULESET"/"

#define FAIL(fmt, args...) do {                                   \
        g_warning("DRES plugin, %s: "fmt, __FUNCTION__, ## args); \
        goto fail;                                                \
    } while (0)

    char *extensions[] = {
        PROLOG_SYSDIR"extensions/fact",
        NULL
    };

    char *rules[] = {
#if 0
        DRES_PLC_PATH,
#else
#if 0
        PROLOG_RULEDIR"hwconfig",
        PROLOG_RULEDIR"devconfig",
        PROLOG_RULEDIR"interface",
        PROLOG_RULEDIR"profile",
        PROLOG_RULEDIR"audio",
#else
        PROLOG_RULEDIR"policy",
#endif
#endif
        NULL
    };
    
    if (!OHM_DEBUG_INIT(resolver))
        FAIL("failed to initialize debugging");

    if ((dres = dres_init(NULL)) == NULL)
        FAIL("failed to initialize DRES library");
    
    if (dres_register_handler(dres, "prolog", prolog_handler) != 0)
        FAIL("failed to register resolver prolog handler");

    if (dres_register_handler(dres, "signal_changed", signal_handler) != 0)
        FAIL("failed to register resolver signal_changed handler");

    if (dres_parse_file(dres, DRES_RULE_PATH))
        FAIL("failed to parse resolver rule file \"%s\"", DRES_RULE_PATH);

    if (prolog_setup(extensions, rules) != 0)
        FAIL("failed to load extensions and rules to prolog interpreter");

    if (dres_finalize(dres) != 0)
        FAIL("failed to finalize resolver setup");

#if 0
    dres_dump_targets(dres);
#endif    

    if (console_init("127.0.0.1:3000"))
        g_warning("resolver plugin: failed to open console");

    if (factstore_init())
        FAIL("factstore initialization failed");
    
    OHM_DEBUG(DBG_RESOLVE, "resolver initialized");

    return;

 fail:
    if (dres) {
        dres_exit(dres);
        dres = NULL;
    }
    exit(1);

    (void)plugin;
#undef FAIL
}


/**
 * plugin_exit:
 **/
static void
plugin_exit(OhmPlugin *plugin)
{
    factstore_exit();

    if (dres) {
        dres_exit(dres);
        dres = NULL;
    }

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
 * prolog_handler
 ********************/
DRES_ACTION(prolog_handler)
{
#define FAIL(ec) do { err = ec; goto fail; } while (0)
#define MAX_FACTS 63
#define MAX_ARGS  32

    prolog_predicate_t   *predicate;
    char                 *pred_name;
    char                 *argv[MAX_ARGS];
    char               ***retval;
    vm_global_t          *g = NULL;
    int                   i, err;

    
    OHM_DEBUG(DBG_RESOLVE, "prolog handler entered...");
    
    if (narg < 1 || args[0].type != DRES_TYPE_STRING)
        return EINVAL;

    retval    = NULL;
    pred_name = args[0].v.s;

    if ((predicate = prolog_lookup(pred_name, narg)) == NULL)
        FAIL(ENOENT);
    
    args++;
    narg--;

    for (i = 0; i < narg; i++) {
        if (args[i].type != DRES_TYPE_STRING)
            FAIL(EINVAL);
        argv[i] = args[i].v.s;
    }
    
    if (!prolog_ainvoke(predicate, &retval, (void **)argv, narg)) {
        prolog_dump(retval);                    /* dump any exceptions */
        FAIL(EINVAL);
    }
    
    OHM_DEBUG(DBG_RESOLVE, "rule engine gave the following results:");
    prolog_dump(retval);

    if ((g = vm_global_alloc(MAX_FACTS)) == NULL)
        FAIL(ENOMEM);
    
    if ((g->nfact = retval_to_facts(retval, g->facts, MAX_FACTS)) < 0)
        FAIL(EINVAL);

    prolog_free(retval);

    rv->type = DRES_TYPE_FACTVAR;
    rv->v.g  = g;
    return 0;
    
 fail:
    if (g)
        vm_global_free(g);
    
    if (retval)
        prolog_free(retval);
    
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

    for (i = 0;  i < factc;  i++) {
        OHM_DEBUG(DBG_SIGNAL, "   fact[%d]: '%s'", i, factv[i]);
    }
}



/*****************************************************************************
 *                        *** misc. helper routines ***                      *
 *****************************************************************************/


#include "console.c"
#include "factstore.c"
 


OHM_PLUGIN_DESCRIPTION("dres",
                       "0.0.0",
                       "krisztian.litkey@nokia.com",
                       OHM_LICENSE_NON_FREE,
                       plugin_init,
                       plugin_exit,
                       NULL);

OHM_PLUGIN_REQUIRES("prolog", "console");

OHM_PLUGIN_PROVIDES_METHODS(dres, 1,
    OHM_EXPORT(update_goal, "resolve")
);

OHM_PLUGIN_REQUIRES_METHODS(dres, 15,
    OHM_IMPORT("prolog.setup"            , prolog_setup),
    OHM_IMPORT("prolog.lookup"           , prolog_lookup),
    OHM_IMPORT("prolog.call"             , prolog_invoke),
    OHM_IMPORT("prolog.vcall"            , prolog_vinvoke),
    OHM_IMPORT("prolog.acall"            , prolog_ainvoke),
    OHM_IMPORT("prolog.free_retval"      , prolog_free),
    OHM_IMPORT("prolog.dump_retval"      , prolog_dump),
    OHM_IMPORT("prolog.prompt"           , prolog_prompt),
    OHM_IMPORT("console.open"            , console_open),
    OHM_IMPORT("console.close"           , console_close),
    OHM_IMPORT("console.write"           , console_write),
    OHM_IMPORT("console.printf"          , console_printf),
    OHM_IMPORT("console.grab"            , console_grab),
    OHM_IMPORT("console.ungrab"          , console_ungrab),
    OHM_IMPORT("signaling.signal_changed", signal_changed)
);

                            

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
