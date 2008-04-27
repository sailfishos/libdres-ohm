#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "dres.h"
#include "parser.h"

#include <prolog/prolog.h>
#include <prolog/ohm-fact.h>
#include <prolog/factmap.h>

#define fatal(ec, fmt, args...) do {                \
        printf("fatal error: " fmt "\n", ## args);  \
        exit(ec);                                   \
    } while (0)


typedef struct {
    char          *name;
    char          *key;
    char         **fields;
    OhmFactStore  *fs;
    factmap_t     *map;
    int          (*filter)(int, char **, void *);
} map_t;



extern int lexer_lineno(void);

static int           rule_engine_init (void);
static OhmFactStore *factstore_init   (void);
static int           factstore_update (OhmFactStore *fs);
static int           prolog_handler   (dres_t *dres, char *name,
                                       dres_action_t *action, void **ret);
static map_t        *factmap_init     (OhmFactStore *fs);
static int           factmap_check    (map_t *map);
static void          command_loop     (dres_t *dres);



#define FACT_PREFIX "com.nokia.policy."
#define F(n) FACT_PREFIX#n

struct fact {
    char    *name;
    OhmFact *fact;
} facts[] = {
    { F(sleeping_request), NULL },
    { F(sleeping_state), NULL },
    { F(battery), NULL },
    { F(idle), NULL },
    { F(min_cpu_frequency), NULL },
    { F(max_cpu_frequency), NULL },
    { F(cpu_frequency), NULL },
    { F(temperature), NULL },
    { F(current_profile), NULL },
    { F(privacy_override), NULL },
    { F(connected), NULL },
    { F(audio_active_policy_group), NULL },
    { F(volume_limit), NULL },
    { F(audio_cork), NULL },
    { F(audio_route), NULL },
    { F(cpu_load), NULL },
    { F(audio_playback_request), NULL },
    { F(audio_playback), NULL },
    { F(current_profile), NULL },
    { F(connected), NULL },
    { NULL, NULL }
};

OhmFactStore *fs;
map_t        *maps;


int
main(int argc, char *argv[])
{
    char   *rulefile;
    dres_t *dres;

    rulefile = argc < 2 ? NULL  : argv[1];

    g_type_init();
    
    
    if (rule_engine_init() != 0)
        fatal(1, "failed to initialize the rule engine");
    
    if ((fs = factstore_init()) == NULL)
        fatal(1, "failed to initialize the fact store");

    if ((maps = factmap_init(fs)) == NULL)
        fatal(1, "failed to initialize factstore prolog mapppings");
    
    if ((dres = dres_init()) == NULL)
        fatal(1, "failed to initialize dres with \"%s\"", rulefile);
    
    if (dres_register_handler(dres, "prolog", prolog_handler) != 0)
        fatal(1, "failed to register DRES prolog handler");

    if (dres_parse_file(dres, rulefile))
        fatal(1, "failed to parse DRES rule file %s", rulefile);

    dres_dump_targets(dres);

    command_loop(dres);

    dres_exit(dres);
    
    return 0;
}


/********************
 * command_loop
 ********************/
static void
command_loop(dres_t *dres)
{
    char *goal, *p, command[128];
    int   status;
    
    printf("Enter target (ie. goal) names to test them.\n");
    printf("Enter prolog to drop into an interactive prolog prompt.\n");
    printf("Enter Control-d or quit to exit.\n");
    while (1) {
        printf("dres> ");
        if (fgets(command, sizeof(command), stdin) == NULL)
            break;
        
        if ((p = strchr(command, '\n')) != NULL)
            *p = '\0';
        
        if (!strcmp(command, "prolog")) {
            prolog_prompt();
            continue;
        }
        
        if (!strcmp(command, "quit"))
            break;


        factstore_update(fs);

        goal = command;
        printf("updating goal '%s'\n", goal);
        if ((status = dres_update_goal(dres, goal)) != 0)
            printf("failed to update goal \"%s\"\n", goal);
    }
}


/********************
 * rule_engine_init
 ********************/
static int
rule_engine_init(void)
{
#define PROLOG_SYSDIR "/usr/share/prolog/"
    char *extensions[] = {
        PROLOG_SYSDIR"extensions/relation",
        PROLOG_SYSDIR"extensions/set",
    };
    int nextension = sizeof(extensions) / sizeof(extensions[0]);

    char *files[] = {
        "prolog/hwconfig",
        "prolog/devconfig",
        "prolog/interface",
        "prolog/profile",
        "prolog/audio",
        "prolog/test"
    };
    int nfile = sizeof(files)/sizeof(files[0]);
    int i;

    /* initialize our prolog library */
    if (prolog_init("test", 0, 0, 0, 0) != 0)
        fatal(1, "failed to initialize prolog library");

    /* load out extensions */
    for (i = 0; i < nextension; i++) {
        DEBUG("loading prolog extension %s...", extensions[i]);
        if (prolog_load_extension(extensions[i]))
            fatal(2, "failed to load %s", extensions[i]);
    }

    /* load our test files */
    for (i = 0; i < nfile; i++) {
        DEBUG("loading prolog ruleset %s...", files[i]);
        if (prolog_load_file(files[i]))
            fatal(2, "failed to load %s", files[i]);
    }
    
    return 0;
}


/********************
 * factstore_init
 ********************/
static OhmFactStore *
factstore_init(void)
{
    OhmFactStore *fs;
    GValue        gval;
    int           i;
    
    if ((fs = ohm_fact_store_get_fact_store()) == NULL)
        return NULL;
    
    for (i = 0; facts[i].name != NULL; i++) {
        if ((facts[i].fact = ohm_fact_new(facts[i].name)) == NULL)
            return NULL;
        gval = ohm_value_from_string("bar");
        ohm_fact_set(facts[i].fact, "foo", &gval);
        if (!ohm_fact_store_insert(fs, facts[i].fact))
            return NULL;
    }
    
    return fs;
}


/********************
 * factstore_update
 ********************/
static int
factstore_update(OhmFactStore *fs)
{
    GValue   gval;
    OhmFact *fact;
    int      i;
    
    for (i = 0; facts[i].name != NULL; i++) {
        
        if (strcmp(facts[i].name, F(temperature)) &&
            strcmp(facts[i].name, F(current_profile)))
            continue;
        
        if ((fact = ohm_fact_new(facts[i].name)) == NULL)
            fatal(1, "could not create fact %s", facts[i].name);
        gval = ohm_value_from_string("barfoo");
        ohm_fact_set(fact, "foobar", &gval);
        if (!ohm_fact_store_insert(fs, fact))
            fatal(1, "failed to insert fact %s to fact store", facts[i].name);
    }
    
    return 0;
}



/********************
 * active_accessory
 ********************/
int
active_accessory(int arity, char **row, void *dummy)
{
#define FIELD_STATE 1
    return (row[FIELD_STATE][0] == '1');        /* hmm, ontology... */
}


/********************
 * factmap_init
 ********************/
static map_t *
factmap_init(OhmFactStore *fs)
{
#define FIELDS(m, ...) static char *m##_fields[] = { __VA_ARGS__, NULL }

#define MAP(m, k, f) {                      \
        .name   = #m,                       \
        .key    = k,                        \
        .fields = m##_fields,               \
        .filter = f                         \
    }

    FIELDS(accessories , "device", "state");
    FIELDS(volume_limit, "group" , "limit");
    FIELDS(audio_route , "group" , "device");

    static map_t maps[] = {
        MAP(accessories , "com.nokia.policy.accessories" , active_accessory),
        MAP(volume_limit, "com.nokia.policy.volume_limit", NULL),
        MAP(audio_route , "com.nokia.policy.audio_route" , NULL),
        { .name = NULL }
    };

    map_t  *m;
    
    for (m = maps; m->name; m++) {
        m->fs  = fs;
        m->map = factmap_create(fs, m->name, m->key, m->fields, m->filter,NULL);

        if (m->map == NULL) {
            DEBUG("failed to create factmap %s for %s", m->name, m->key);
            return NULL;
        }
        else
            DEBUG("created factmap %s -> %s", m->name, m->key);
    }
    
    return maps;
}


/********************
 * factmap_check
 ********************/
static int
factmap_check(map_t *map)
{
    map_t *m;
    int    status = 0;
    
    for (m = map; m->name; m++) {
        DEBUG("updating fact map %s...", m->name);
        if (factmap_update(m->map) != 0)
            status = 1;
    }

    return status;
}


/********************
 * prolog_handler
 ********************/
static int
prolog_handler(dres_t *dres, char *name, dres_action_t *action, void **ret)
{
    prolog_predicate_t *predicates, *p, *pred;
    char               *pred_name, ***actions;
    char                buf[64];
    
    if ((predicates = prolog_predicates(NULL)) == NULL) {
        DEBUG("failed to determine predicate table");
        return ENOENT;
    }

    pred_name = dres_name(dres, action->arguments[0], buf, sizeof(buf));
    
    pred = NULL;
    for (p = predicates; p->name; p++) {
        if (!strcmp(p->name, pred_name)) {
            DEBUG("found exported predicate: %s%s%s/%d (0x%x)",
                  p->module ?: "", p->module ? ":" : "",
                  p->name, p->arity, (unsigned int)p->predicate);
            pred = p;
            break;
        }
    }
    
    if (pred == NULL) {
        DEBUG("cold not find prolog predicate %s", pred_name);
        return ENOENT;
    }
    
    DEBUG("calling prolog predicate %s...", pred_name);


    factmap_check(maps);
    
    if (!prolog_call(pred, &actions))
        return EINVAL;

    printf("rule engine gave the following policy decisions:\n");
    prolog_dump_actions(actions);

    return 0;
}




void
yyerror(dres_t *dres, const char *msg)
{
    printf("error: %s, on line %d near input %s\n", msg, lexer_lineno(),
           yylval.string);
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
