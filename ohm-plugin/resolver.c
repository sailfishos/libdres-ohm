/******************************************************************************/
/*  This file is part of dres the resource policy dependency resolver.        */
/*                                                                            */
/*  Copyright (C) 2010 Nokia Corporation.                                     */
/*                                                                            */
/*  This library is free software; you can redistribute                       */
/*  it and/or modify it under the terms of the GNU Lesser General Public      */
/*  License as published by the Free Software Foundation                      */
/*  version 2.1 of the License.                                               */
/*                                                                            */
/*  This library is distributed in the hope that it will be useful,           */
/*  but WITHOUT ANY WARRANTY; without even the implied warranty of            */
/*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU          */
/*  Lesser General Public License for more details.                           */
/*                                                                            */
/*  You should have received a copy of the GNU Lesser General Public          */
/*  License along with this library; if not, write to the Free Software       */
/*  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  */
/*  USA.                                                                      */
/******************************************************************************/

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
/*#include <prolog/prolog.h>*/

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
static int DBG_RESOLVE, DBG_PROLOG, DBG_SIGNAL, DBG_FACTS, DBG_DELAY;

OHM_DEBUG_PLUGIN(resolver,
    OHM_DEBUG_FLAG("resolver", "dependency resolving", &DBG_RESOLVE),
    OHM_DEBUG_FLAG("prolog"  , "prolog handler"      , &DBG_PROLOG),
    OHM_DEBUG_FLAG("signal"  , "decision emission"   , &DBG_SIGNAL),
    OHM_DEBUG_FLAG("facts"   , "fact handling"       , &DBG_FACTS),
    OHM_DEBUG_FLAG("delay"   , "delayed execution"   , &DBG_DELAY)
);


/* rule engine methods */
OHM_IMPORTABLE(void, rules_free_result, (void *retval));
OHM_IMPORTABLE(void, rules_dump_result, (void *retval));
OHM_IMPORTABLE(void, rules_prompt     , (void));
OHM_IMPORTABLE(int , rules_trace      , (char *));

OHM_IMPORTABLE(int , rule_find        , (char *name, int arity));
OHM_IMPORTABLE(int , rule_eval        ,
               (int rule, void *retval, void **args, int narg));
OHM_IMPORTABLE(void, rule_statistics  , (char *));

static void plugin_exit(OhmPlugin *plugin);

static int  rules_init(void);
static void rules_exit(void);
static int  rule_lookup(const char *name, int arity);

static GHashTable *ruletbl;


static int  resolver_init(const char *ruleset);
static void resolver_exit(void);

static dres_handler_t unknown_handler;


/* signaling */
typedef void (*completion_cb_t)(char *id, char *argt, void **argv);
typedef void (*delay_cb_t)(char *id, char *argt, void **argv);

OHM_IMPORTABLE(int, signal_changed,    (char *signal, int transid,
                                        int factc, char **factv,
                                        completion_cb_t callback,
                                        unsigned long timeout));
OHM_IMPORTABLE(void, completion_cb,    (int transid, int success));

OHM_IMPORTABLE(int, delay_execution,   (unsigned long delay, char *id,
                                        int restart, char *cb_name,
                                        delay_cb_t cb,char *argt,void **argv));
OHM_IMPORTABLE(int, delay_cancel,      (char *id));
OHM_IMPORTABLE(void, delay_cb,         (char *name, char *argt, void **argv));

static void delayed_resolve(char *id, char *argt, void **argv);

static void dump_signal_changed_args(char *signame, int transid, int factc,
                                     char**factv, completion_cb_t callback,
                                     unsigned long timeout);
static void dump_delayed_execution_args(char *function, int delay, char * id,
                                        char *cb_name, char *argt,void **argv);

static int  retval_to_facts(char ***objects, OhmFact **facts, int max);



DRES_ACTION(rule_handler);
DRES_ACTION(signal_handler);
DRES_ACTION(delay_handler);
DRES_ACTION(cancel_handler);
DRES_ACTION(fallback_handler);

static dres_t *dres;

typedef struct {
    const char     *name;
    dres_handler_t  handler;
} handler_t;

static handler_t handlers[] = {
    { "prolog"         , rule_handler   },
    { "rule"           , rule_handler   },
    { "signal_changed" , signal_handler },
    { "delay_execution", delay_handler  },
    { "delay_cancel"   , cancel_handler },
    { NULL             , NULL           }
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
    char *console = (char *)ohm_plugin_get_param(plugin, "console");
    char *ruleset = (char *)ohm_plugin_get_param(plugin, "ruleset");

    if (!OHM_DEBUG_INIT(resolver))
        OHM_WARNING("resolver plugin failed to initialize debugging");
    
    if (console == NULL)
        console = DEFAULT_CONSOLE;
    
    if (ruleset == NULL)
        ruleset = DEFAULT_RULESET;
    
    if (resolver_init(ruleset) != 0 || rules_init() != 0 ||
        factstore_init() != 0 || console_init(console) != 0) {
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
    (void)plugin;

    factstore_exit();
    resolver_exit();
    rules_exit();
    console_exit();
}



/********************
 * dres_parse_error
 ********************/
void
dres_parse_error(dres_t *dres, int lineno, const char *msg, const char *token)
{
    (void)dres;
    (void)token;
    
    g_warning("compilation error: %s, on line %d\n", msg, lineno);
    exit(1);
}


/********************
 * logger
 ********************/
static void
logger(dres_log_level_t level, const char *format, va_list ap)
{
    OhmLogLevel l;

    switch (level) {
    case DRES_LOG_FATAL:    
    case DRES_LOG_ERROR:   l = OHM_LOG_ERROR;   break;
    case DRES_LOG_WARNING: l = OHM_LOG_WARNING; break;
    case DRES_LOG_NOTICE:
    case DRES_LOG_INFO:    l = OHM_LOG_INFO;    break;
    default:                                    return;
    }

    ohm_logv(l, format, ap);
}


/********************
 * resolver_init
 ********************/
static int
resolver_init(const char *ruleset)
{
    handler_t *h;

    OHM_INFO("resolver: using ruleset %s", ruleset);

    dres_set_logger(logger);

    /* initialize resolver with our ruleset */
    OHM_DEBUG(DBG_RESOLVE, "Initializing resolver...");
    if ((dres = dres_open((char *)ruleset)) == NULL) {
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
    
    unknown_handler = dres_fallback_handler(dres, fallback_handler);
    

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
    int   status;
    char *result;

    OHM_DEBUG(DBG_RESOLVE, "resolving goal '%s'", goal);

    status = dres_update_goal(dres, goal, locals);

    if      (status >  0) result = "succeeded";
    else if (status == 0) result = "failed";
    else                  result = "failed with an exception";

    OHM_DEBUG(DBG_RESOLVE, "resolving goal '%s' %s", goal, result);
    
    return status;
}


/*****************************************************************************
 *                          *** DRES action handlers ***                     *
 *****************************************************************************/


/********************
 * rule_handler
 ********************/
DRES_ACTION(rule_handler)
{
#define FAIL(ec) do { status = ec; goto fail; } while (0)
#define MAX_FACTS 63
#define MAX_ARGS  (32*2)

    int            rule;
    char          *rule_name;
    char          *argv[MAX_ARGS];
    char        ***retval;
    vm_global_t   *g = NULL;
    int            i, status;

    (void)data;
    (void)name;
    
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
    
    
    status = rule_eval(rule, &retval, (void **)argv, narg);

    if (status < 0) {
        rules_dump_result(retval);                  /* dump exceptions */
        FAIL(status);
    }
    else if (!status)                               /* predicate failure */
        FAIL(FALSE);
    
    if (OHM_LOGGED(INFO))
        rules_dump_result(retval);

    if ((g = vm_global_alloc(MAX_FACTS)) == NULL)
        FAIL(-ENOMEM);
    
    if ((g->nfact = retval_to_facts(retval, g->facts, MAX_FACTS)) < 0)
        FAIL(-EINVAL);

    rules_free_result(retval);

    rv->type = DRES_TYPE_FACTVAR;
    rv->v.g  = g;
    DRES_ACTION_SUCCEED;
    
 fail:
    if (g)
        vm_global_free(g);
    
    if (retval)
        rules_free_result(retval);
    
    DRES_ACTION_ERROR(status);

#undef MAX_FACTS
#undef MAX_ARGS
}


/********************
 * fallback_handler
 ********************/
DRES_ACTION(fallback_handler)
{
#define FAIL(ec) do { status = ec; goto fail; } while (0)
#define MAX_FACTS 63
#define MAX_ARGS  (32*2)

    int            rule;
    char          *rule_name;
    char          *argv[MAX_ARGS];
    char        ***retval;
    vm_global_t   *g = NULL;
    int            i, status;

    (void)data;
    
    OHM_DEBUG(DBG_RESOLVE, "Fallback handler called for '%s'...", name);
    
    retval    = NULL;
    rule_name = name;

    if ((rule = rule_lookup(rule_name, narg + 1)) < 0) {
        if (unknown_handler)
            return unknown_handler(data, name, args, narg, rv);
        else
            DRES_ACTION_ERROR(EINVAL);
    }
    
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
    
    status = rule_eval(rule, &retval, (void **)argv, narg);

    if (status < 0) {
        rules_dump_result(retval);                  /* dump exceptions */
        FAIL(status);
    }
    else if (!status)                               /* predicate failure */
        FAIL(FALSE);
    
    if (OHM_LOGGED(INFO))
        rules_dump_result(retval);

    if ((g = vm_global_alloc(MAX_FACTS)) == NULL)
        FAIL(-ENOMEM);
    
    if ((g->nfact = retval_to_facts(retval, g->facts, MAX_FACTS)) < 0)
        FAIL(-EINVAL);

    rules_free_result(retval);

    rv->type = DRES_TYPE_FACTVAR;
    rv->v.g  = g;
    DRES_ACTION_SUCCEED;
    
 fail:
    if (g)
        vm_global_free(g);
    
    if (retval)
        rules_free_result(retval);
    
    DRES_ACTION_ERROR(status);

#undef MAX_FACTS
#undef MAX_ARGS
}


/**************************************
 * macros to fetch arguments
 **************************************/
#define GET_ARG(var, n, f, t) do {               \
        if (args[(n)].type == t)                 \
            (var) = args[(n)].v.f;               \
        else {                                   \
            if (args[(n)].type == DRES_TYPE_NIL) \
                (var) = 0;                       \
            else                                 \
                DRES_ACTION_ERROR(EINVAL);       \
        }                                        \
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
                DRES_ACTION_ERROR(EINVAL);                              \
        }                                                               \
    } while (0)


/********************
 * signal_handler
 ********************/
DRES_ACTION(signal_handler)
{
#define MAX_FACTS  64
#define MAX_LENGTH 64
#define TIMEOUT    (5 * 1000)
    
    char *signal_name, *cb_name;
    int   txid;
    int   nfact, i;
    char *facts[MAX_FACTS + 1];
    char  buf  [MAX_FACTS * MAX_LENGTH];
    char *p;
    char *signature;
    int   success;

    (void)name;
    (void)data;
    
    if (narg < 4)
        DRES_ACTION_ERROR(EINVAL);
    
    GET_STRING(0, signal_name, "");
    GET_STRING(1, cb_name, "");
    GET_INTEGER(2, txid);

    nfact = narg - 3;
    args += 3;
    narg -= 3;
        
    OHM_DEBUG(DBG_SIGNAL, "signal='%s', cb='%s' txid=%d",
              signal_name, cb_name, txid);
    

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
            OHM_DEBUG(DBG_SIGNAL, "could not resolve signal.");
            success = FALSE;
        }
    }

    OHM_DEBUG(DBG_SIGNAL, "signal_changed() %s", success?"succeeded":"failed");

    rv->type = DRES_TYPE_INTEGER;
    rv->v.i  = 0;

    if (success)
        DRES_ACTION_SUCCEED;
    else
        DRES_ACTION_ERROR(EIO);

#undef MAX_LENGTH
#undef MAX_FACTS
#undef TIMEOUT
}


/********************
 * delay_handler
 ********************/
DRES_ACTION(delay_handler)
{
#define MAX_ARGS   64

    char        *signature = (char *)delay_cb_SIGNATURE;
    int          delay;
    char        *delay_str;
    char        *id;
    char        *cb_name;
    delay_cb_t   cb;
    char         argt[MAX_ARGS + 1];
    void        *argv[MAX_ARGS];
    int          i;
    vm_global_t *g;
    const char  *nm;
    char        *e;
    int          success;

    (void)name;
    (void)data;
    
    if (narg < 3)
        DRES_ACTION_ERROR(EINVAL);
    
    switch (args[0].type) {

    case DRES_TYPE_INTEGER:
        delay = args[0].v.i;
        break;

    case DRES_TYPE_STRING:
        GET_STRING(0, delay_str, "_");
        delay = strtol(delay_str, &e, 10);

        if (*delay_str && !*e)
            break;

        /* intentional fall trough */
    default:
        DRES_ACTION_ERROR(EINVAL);
        break;
    }
   
    GET_STRING  (1, id, "<unknown>");
    GET_STRING  (2, cb_name, "");
    

    args += 3;
    narg -= 3;

    if (delay < 0 || delay > 3600000 || narg > MAX_ARGS)
        DRES_ACTION_ERROR(EINVAL);
    
    /*
     * This approach assumes that no module get unloaded. IMHO this is good
     * compromise between multiple lookups of the same CB routine and the
     * assumption that nothing disappears from under us
     * In additition it provides a convenient way to hide the dirty details
     * of the 'resolve' method and alike
     */

    if (!strcmp(cb_name, "")) {
        OHM_DEBUG(DBG_DELAY, "silently ignoring delayed execution request "
                  "'%s' with empty callback", id);
        DRES_ACTION_SUCCEED;
    }
    else if (!strcmp(cb_name, "resolve")) {
        /* check the arglist wheter it starts with a rule name
           and the rest is series of name,value pairs */

        if (!(narg & 1) || narg < 1 || args[0].type != DRES_TYPE_STRING)
            DRES_ACTION_ERROR(EINVAL);

        for (i = 1; i < narg-1; i += 2) {
            if (args[i].type != DRES_TYPE_STRING)
                DRES_ACTION_ERROR(EINVAL);
        }

        cb = delayed_resolve;
    }
    else if (!ohm_module_find_method(cb_name, &signature, (void *)&cb)) {
        OHM_DEBUG(DBG_DELAY, "could not resolve callback '%s'", cb_name);
        DRES_ACTION_ERROR(EINVAL);
    }

    memset(argt, 0, sizeof(argt));

    for (i = 0; i < narg; i++) {
        switch (args[i].type) {

        case DRES_TYPE_FACTVAR:
            g = args[i].v.g;

            if (g->nfact < 1)
                break;
            if (!(nm = ohm_structure_get_name(OHM_STRUCTURE(g->facts[0]))))
                break;
     
            argt[i] = 's';
            argv[i] = (void *)nm;
            break;

        case DRES_TYPE_NIL:
            argt[i] = 's';
            argv[i] = "";
            break;

        case DRES_TYPE_STRING:
            argt[i] = 's';
            argv[i] = args[i].v.s;
            break;

        case DRES_TYPE_INTEGER:
            argt[i] = 'i';
            argv[i] = &args[i].v.i;
            break;

        case DRES_TYPE_DOUBLE:
            argt[i] = 'f';
            argv[i] = &args[i].v.d;
            break;

        default:
            OHM_INFO("We are fucked up");
            DRES_ACTION_ERROR(EINVAL);
            break;
        }
        
    }

    dump_delayed_execution_args("delay_execution", delay, id,
                                cb_name, argt, argv);

    success = delay_execution(delay, id, TRUE, cb_name, cb, argt, argv);

    rv->type = DRES_TYPE_INTEGER;
    rv->v.i  = 0;

    if (success)
        DRES_ACTION_SUCCEED;
    else
        DRES_ACTION_ERROR(EIO);

#undef MAX_ARGS
}


/********************
 * cancel_handler
 ********************/
DRES_ACTION(cancel_handler)
{
    char        *id;
    int          success;

    (void)name;
    (void)data;
    
    if (narg != 1)
        DRES_ACTION_ERROR(EINVAL);
    
    GET_STRING(0, id, "<unknown>");


    OHM_DEBUG(DBG_DELAY, "calling delay_cancel('%s')", id);

    success = delay_cancel(id);

    if (!success)
        OHM_DEBUG(DBG_DELAY, "delay_cancel('%s') failed", id);


    rv->type = DRES_TYPE_INTEGER;
    rv->v.i  = 0;

    DRES_ACTION_SUCCEED;
}

static void delayed_resolve(char *id, char *argt, void **argv)
{
#define MAX_ARG 64

    int   argc;
    char *target;
    char *vars[MAX_ARG * 3];
    int   i, j;
    

    if (id && argt && argv) {
        argc = strlen(argt);

        if (argc > 0 && argc < MAX_ARG && (argc & 1) && argt[0] == 's') {
            target = argv[0];

            dump_delayed_execution_args("delayed_resolve", -1, id,
                                        "resolve", argt, argv);

            for (i = 1, j = 0;   i < argc;   i += 2) {
                if (argt[i] != 's')
                    goto failed;

                vars[j++] = (char *)argv[i];
                vars[j++] = (char *)((int)argt[i+1]);
                vars[j++] = (char *)argv[i+1];
            }

            vars[j++] = NULL;

            update_goal(target, vars);

            return;
        }
    }


 failed:
    OHM_DEBUG(DBG_DELAY, "invalid argument list");


#undef MAX_ARG
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

static void dump_delayed_execution_args(char *function, int delay, char * id, 
                                        char *cb_name, char *argt, void **argv)
{
    char  buf[256];
    int   i,n;

    OHM_DEBUG(DBG_DELAY, "calling %s(%d, '%s', 1, %s, '%s', %p)",
              function, delay, id, cb_name, argt, argv);

    for (i = 0, n = strlen(argt);   i < n;   i++) {

        switch (argt[i]) {
        case 's':  snprintf(buf, sizeof(buf), "%s", (char *)argv[i]);    break;
        case 'i':  snprintf(buf, sizeof(buf), "%d", *(int *)argv[i]);    break;
        case 'f':  snprintf(buf, sizeof(buf), "%lf", *(double*)argv[i]); break;
        default:   snprintf(buf, sizeof(buf), "<invalid>");              break;
        }

        OHM_DEBUG(DBG_DELAY, "   argv[%d]: %s", i, buf);
    }
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
     * Notes: We could avoid this hash table lookup if it was possible to
     *     associate some 'user' data to DRES action invocations. We could
     *     then associate the rule ID with each invocation.
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



#define MAX_ARGS  (32*2)

#include "console.c"
#include "factstore.c"

#undef MAX_ARGS 


OHM_PLUGIN_DESCRIPTION("dres",
                       "0.0.1",
                       "krisztian.litkey@nokia.com",
                       OHM_LICENSE_NON_FREE,
                       plugin_init,
                       plugin_exit,
                       NULL);

OHM_PLUGIN_PROVIDES_METHODS(dres, 2,
    OHM_EXPORT(update_goal, "resolve"),
    OHM_EXPORT(add_command, "add_command")
);

OHM_PLUGIN_REQUIRES_METHODS(dres, 10,
    OHM_IMPORT("rule_engine.find"      , rule_find),
    OHM_IMPORT("rule_engine.eval"      , rule_eval),
    OHM_IMPORT("rule_engine.free"      , rules_free_result),
    OHM_IMPORT("rule_engine.dump"      , rules_dump_result),
    OHM_IMPORT("rule_engine.prompt"    , rules_prompt),
    OHM_IMPORT("rule_engine.trace"     , rules_trace),
    OHM_IMPORT("rule_engine.statistics", rule_statistics),

    OHM_IMPORT("signaling.signal_changed", signal_changed),

    OHM_IMPORT("delay.delay_execution", delay_execution),
    OHM_IMPORT("delay.delay_cancel"   , delay_cancel)
);

                            

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
