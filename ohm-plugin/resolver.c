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
OHM_IMPORTABLE(void, prolog_shell , (void));

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
#define DRES_RULE_PATH "/tmp/policy/policy.dres"
#define DRES_PLC_PATH  "/tmp/policy/policy.plc"
#define PROLOG_SYSDIR  "/usr/share/prolog/"
#define PROLOG_RULEDIR "/tmp/policy/prolog/"

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
        PROLOG_RULEDIR"hwconfig",
        PROLOG_RULEDIR"devconfig",
        PROLOG_RULEDIR"interface",
        PROLOG_RULEDIR"profile",
        PROLOG_RULEDIR"audio",
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

    dres_dump_targets(dres);
    
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
    
    /*
     * Notes:
     *
     *   Strictly speaking passing narg to the prolog predicate is 
     *   is semantically wrong. This handler takes the name of the
     *   predicate and the arguments to be passed to it. Thus narg
     *   equals to the number of predicate arguments + 1.
     *
     *   However, according to our prolog predicate calling conventions
     *   each prolog predicate takes an extra argument which is used to
     *   pass the predicate results back from Prolog to C. This makes
     *   the arity of every exported prolog predicate equal to the
     *   number of arguments + 1. The check in prolog.c:prolog_*call
     *   checks narg against predicate->arity which is equally wrong
     *   but the bugs cancel each other out.
     *
     *   XXX TODO: Eventually we must fix both bugs, but I cannot do
     *       it in libprolog safely because I do not want to touch
     *       the original (old, currently master) branch of dres
     *       (now that we have tagged it as alpha release) and it has
     *       the same bug. I will fix it once the new runtime is merged
     *       in to master.
     */
    
    retval = NULL;        /* see the notes above explaining why + 1 */
    if (!prolog_ainvoke(predicate, &retval, (void **)argv, narg + 1))
        FAIL(EINVAL);
    
    OHM_DEBUG(DBG_RESOLVE, "rule engine gave the following results:");
    prolog_dump(retval);

    if ((g = vm_global_alloc(MAX_FACTS)) == NULL)
        FAIL(ENOMEM);
    
    if ((g->nfact = retval_to_facts(retval, g->facts, MAX_FACTS)) < 0)
        FAIL(EINVAL);


    rv->type = DRES_TYPE_FACTVAR;
    rv->v.g  = g;
    return 0;
    
 fail:
    if (g)
        vm_global_free(g);
    
    if (retval)
        prolog_free_actions(retval);
    
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
 *                           *** old action handlers ***                     *
 *****************************************************************************/

#if 0



/********************
 * action_argument
 ********************/
static char *
action_argument(dres_t *dres, int argument)
{
    dres_variable_t *var;
    char             name[64], *value;

    
    dres_name(dres, argument, name, sizeof(name));
        
    switch (name[0]) {
    case '$':
        if ((var = dres_lookup_variable(dres, argument)) == NULL ||
            !dres_var_get_field(var->var, DRES_VAR_FIELD, NULL,
                                VAR_STRING, &value))
            return NULL;
        return value;
        
    case '&':
        if ((value = dres_scope_getvar(dres->scope, name + 1)) == NULL)
            return NULL;
        return value;
        
    case '<':
        return NULL;
        
    default:
        return STRDUP(name);
    }
}


/********************
 * action_arguments
 ********************/
static int
action_arguments(dres_t *dres, dres_action_t *action, char **args, int narg)
{
    int i;
    
    for (i = 1; i < action->nargument; i++)
        if ((args[i-1] = action_argument(dres, action->arguments[i])) == NULL)
            goto fail;

    return i;
    
 fail:
    narg = i;
    for (i = 0; narg - 1; i++)
        if (args[i] != NULL)
            FREE(args[i]);
    
    return -1;
}


static int
prolog_handler(dres_t *dres, char *actname, dres_action_t *action, void **ret)
{
#define FAIL(ec) do { err = ec; goto fail; } while (0)
#define MAX_FACTS 63
    prolog_predicate_t *predicate;
    char                name[64];
    char             ***retval;
    OhmFact           **facts = NULL;
    int                 nfact, err;
    char               *args[32];
    int                 i, narg;

    OHM_DEBUG(DBG_RESOLVE, "prolog handler entered...");
    
    if (action->nargument < 1)
        return EINVAL;

    narg = 0;

    name[0] = '\0';
    dres_name(dres, action->arguments[0], name, sizeof(name));
    
    if ((predicate = prolog_lookup(name, action->nargument)) == NULL)
        FAIL(ENOENT);

    if ((narg = action_arguments(dres, action, args, narg)) < 0)
        FAIL(EINVAL);

    if (!prolog_ainvoke(predicate, &retval, (void **)args, narg))
        FAIL(EINVAL);

    OHM_DEBUG(DBG_RESOLVE, "rule engine gave the following results:");
    prolog_dump(retval);

    if ((facts = ALLOC_ARR(OhmFact *, MAX_FACTS + 1)) == NULL)
        FAIL(ENOMEM);

    if ((nfact = retval_to_facts(retval, facts, MAX_FACTS)) < 0)
        FAIL(EINVAL);
    
    facts[nfact] = NULL;
    
    if (ret == NULL)
        FAIL(0);                     /* kludge: free facts and return 0 */
    
    *ret = facts;
    return 0;
    
 fail:
    if (facts) {
        for (i = 0; i < nfact; i++)
            g_object_unref(facts[i]);
        FREE(facts);
    }
    for (i = 0; i < narg; i++)
        FREE(args[i]);
    
    return err;

#undef MAX_FACTS
}


static
char *getarg(dres_t *dres, dres_action_t *action,
             int argidx, char *namebuf, int len)
{
    dres_variable_t  *var;
    char             *value;

    value = "";

    if (argidx < 0 || argidx >= action->nvariable) {
        namebuf[0] = '\0';
        dres_name(dres, action->arguments[argidx], namebuf, len);

        switch (namebuf[0]) {

        case '&':
            if ((value = dres_scope_getvar(dres->scope, namebuf+1)) == NULL)
                value = "";
            break;

        case '$':
            value = "";
            if ((var = dres_lookup_variable(dres, action->arguments[argidx]))){
                dres_var_get_field(var->var, "value",NULL, VAR_STRING, &value);
            }
            break;

        default:
            value = namebuf;
            break;
        }
    }

    return value;
}


static int
signal_handler(dres_t *dres, char *name, dres_action_t *action, void **ret)
{
#define MAX_FACTS 128
#define MAX_LENGTH 64
    dres_variable_t  *var;
    char             *signature;
    unsigned long     timeout;
    int               factc;
    char              signal_name[MAX_LENGTH];
    char             *cb_name;
    char             *trid_str;
    int               trid;
    char             *prefix;
    char              arg[MAX_LENGTH];
    char             *factv[MAX_FACTS + 1];
    char              buf[MAX_FACTS * MAX_LENGTH];
    char              namebuf[MAX_LENGTH];
    char              tridbuf[MAX_LENGTH];
    char             *p;
    int               i;
    int               offs;
    int               success;
    
    if (ret != NULL)
        *ret = NULL;

    factc = action->nargument - 2;

    if (factc < 1 || factc > MAX_FACTS)
        return EINVAL;
    
    signal_name[0] = '\0';
    dres_name(dres, action->arguments[0], signal_name, MAX_LENGTH);

    cb_name  = getarg(dres, action, 1, namebuf, MAX_LENGTH);
    trid_str = getarg(dres, action, 2, tridbuf, MAX_LENGTH);

    trid  = strtol(trid_str, NULL, 10);

    OHM_DEBUG(DBG_SIGNAL, "%s(): cb_name='%s' tridstr='%s' trid=%d\n",
              __FUNCTION__, cb_name, trid_str, trid);
    
    timeout = 5 * 1000;
    prefix  = dres_get_prefix(dres);

    for (p = buf, i = 0;   i < factc;   i++) {

        dres_name(dres, action->arguments[i+2], arg, MAX_LENGTH);
            
        switch (arg[0]) {

        case '$':
            offs = 1;
            goto copy_string_arg;

        case '&':
            factv[i] = "";

            if ((var = dres_lookup_variable(dres, action->arguments[i+2]))) {
                dres_var_get_field(var->var, "value", NULL,
                                   VAR_STRING, &factv[i]);
            }
            break;

        default:
            offs = 0;
            /* intentional fall trough */

        copy_string_arg:
            factv[i] = p;
            p += snprintf(p, MAX_LENGTH, "%s%s",
                          strchr(arg+offs, '.') ? "" : prefix, arg+offs) + 1;
            break;
        }
    }
    factv[factc] = NULL;

    if (cb_name[0] == '\0') {
        dump_signal_changed_args(signal_name, 0, factc,factv, NULL, timeout);
        success = signal_changed(signal_name, 0, factc,factv, NULL, timeout);
    }
    else {
        signature = (char *)completion_cb_SIGNATURE;

        if (ohm_module_find_method(cb_name,&signature,(void *)&completion_cb)){
            dump_signal_changed_args(signal_name, trid, factc,factv,
                                     completion_cb, timeout);
            success = signal_changed(signal_name, trid, factc,factv,
                                     completion_cb, timeout);
        }
        else {
            OHM_DEBUG(DBG_SIGNAL, "could not resolve signal.\n");
            success = FALSE;
        }
    }

    return success ? 0 : EINVAL;

#undef MAX_LENGTH
#undef MAX_FACTS

}

#endif



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
    OHM_IMPORT("prolog.shell"            , prolog_shell),
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
