#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <dres/dres.h>

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

static int           rule_engine_init (char *pldir);
static OhmFactStore *factstore_init   (void);
static int           prolog_handler   (dres_t *dres, char *name,
                                       dres_action_t *action, void **ret);
static int           objects_to_facts (char *name, char ***objects,
                                       OhmFact ***factptr);

static map_t        *factmap_init     (OhmFactStore *fs);
static int           factmap_check    (map_t *map);
static void          factmap_vardump  (map_t *map, char *factname);
static void          command_loop     (dres_t *dres);



#define FACT_PREFIX "com.nokia.policy"
#define F(n) FACT_PREFIX"."#n
#define MEND {NULL, NULL}

struct member {
    char *name;
    char *value;
};

struct member empty_member[] = {{"value", ""}, MEND};
struct member profile_member[] = {{"value", "general"}, MEND};
struct member privacy_member[] = {{"value", "default"}, MEND};

struct member ihf_member[] = {{"device", "ihf"}, {"state", "1"}, MEND};
struct member bt_member[] = {{"device", "bluetooth"}, {"state", "0"}, MEND};
struct member hs_member[] = {{"device", "headset"}, {"state", "0"}, MEND};
struct member hp_member[] = {{"device", "headphone"}, {"state", "0"}, MEND};
struct member earp_member[] = {{"device", "earpiece"}, {"state", "1"}, MEND};
struct member mic_member[] = {{"device", "microphone"}, {"state", "1"}, MEND};
struct member hm_member[] = {{"device", "headmike"}, {"state", "0"}, MEND};

struct member act_cscall_member[] = {{"group", "cscall"}, {"state", "0"},MEND};
struct member act_ring_member[] = {{"group", "ringtone"}, {"state", "0"},MEND};
struct member act_ipcall_member[] = {{"group", "ipcall"}, {"state", "0"},MEND};
struct member act_player_member[] = {{"group", "player"}, {"state", "0"},MEND};
struct member act_fmradio_member[] = {{"group", "fmradio"},{"state","0"},MEND};
struct member act_other_member[] = {{"group","othermedia"},{"state","1"},MEND};

struct member sinkrt_member[] = {{"type", "sink"}, {"device", "ihf"}, MEND};
struct member sourcert_member[] = {{"type", "source"},{"device", "microphone"},
                                   MEND};

struct member lim_cscall_member[]={{"group", "cscall"}, {"limit", "100"},MEND};
struct member lim_ring_member[]={{"group", "ringtone"}, {"limit", "100"},MEND};
struct member lim_ipcall_member[]={{"group", "ipcall"}, {"limit", "100"},MEND};
struct member lim_player_member[]={{"group", "player"}, {"limit", "100"},MEND};
struct member lim_fmradio_member[]={{"group", "fmradio"},{"limit","100"},MEND};
struct member lim_other_member[]={{"group","othermedia"},{"limit","100"},MEND};

struct member cork_cscall_member[] = {{"group", "cscall"},
                                      {"cork", "unkorked"},MEND};
struct member cork_ring_member[] = {{"group", "ringtone"},
                                    {"cork", "unkorked"},MEND};
struct member cork_ipcall_member[] = {{"group", "ipcall"},
                                      {"cork", "unkorked"},MEND};
struct member cork_player_member[] = {{"group", "player"},
                                      {"cork", "unkorked"},MEND};
struct member cork_fmradio_member[] = {{"group", "fmradio"},
                                       {"cork","unkorked"},MEND};
struct member cork_other_member[] = {{"group","othermedia"},
                                     {"cork","unkorked"},MEND};


struct fact {
    char           *name;
    struct member  *member;
    OhmFact        *fact;
} facts[] = {
    { F(sleeping_request), empty_member, NULL },
    { F(sleeping_state), empty_member, NULL },
    { F(battery), empty_member, NULL },
    { F(idle), empty_member, NULL },
    { F(min_cpu_frequency), empty_member, NULL },
    { F(max_cpu_frequency), empty_member, NULL },
    { F(cpu_frequency), empty_member, NULL },
    { F(temperature), empty_member, NULL },
    { F(current_profile), profile_member, NULL },
    { F(privacy_override), privacy_member, NULL },

    { F(accessories), ihf_member, NULL },
    { F(accessories), bt_member, NULL },
    { F(accessories), hs_member, NULL },
    { F(accessories), hp_member, NULL },
    { F(accessories), earp_member, NULL },
    { F(accessories), mic_member, NULL },
    { F(accessories), hm_member, NULL },

    { F(audio_active_policy_group), act_cscall_member, NULL },
    { F(audio_active_policy_group), act_ring_member, NULL },
    { F(audio_active_policy_group), act_ipcall_member, NULL },
    { F(audio_active_policy_group), act_player_member, NULL },
    { F(audio_active_policy_group), act_fmradio_member, NULL },
    { F(audio_active_policy_group), act_other_member, NULL },

    { F(audio_route), sinkrt_member, NULL },
    { F(audio_route), sourcert_member, NULL },

    { F(volume_limit), lim_cscall_member, NULL },
    { F(volume_limit), lim_ring_member, NULL },
    { F(volume_limit), lim_ipcall_member, NULL },
    { F(volume_limit), lim_player_member, NULL },
    { F(volume_limit), lim_fmradio_member, NULL },
    { F(volume_limit), lim_other_member, NULL },

    { F(audio_cork), cork_cscall_member, NULL },
    { F(audio_cork), cork_ring_member, NULL },
    { F(audio_cork), cork_ipcall_member, NULL },
    { F(audio_cork), cork_player_member, NULL },
    { F(audio_cork), cork_fmradio_member, NULL },
    { F(audio_cork), cork_other_member, NULL },

    { F(cpu_load), empty_member, NULL },
    { F(audio_playback), empty_member, NULL },

    { F(audio_playback_request), empty_member, NULL },
    { NULL, NULL, NULL}
};

OhmFactStore *fs;
map_t        *maps;


int
main(int argc, char *argv[])
{
    char   *rulefile, *pldir;
    dres_t *dres;

    rulefile = argc < 2 ? NULL : argv[1];
    pldir    = argc < 3 ? "../prolog" : argv[2];

    g_type_init();
    
    
    if (rule_engine_init(pldir) != 0)
        fatal(1, "failed to initialize the rule engine");
    
    if ((fs = factstore_init()) == NULL)
        fatal(1, "failed to initialize the fact store");

    if ((maps = factmap_init(fs)) == NULL)
        fatal(1, "failed to initialize factstore prolog mapppings");
    
    if ((dres = dres_init(FACT_PREFIX)) == NULL)
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
    struct fact   *def;
    struct member *m;
    GValue         gval;
    char          *str, *name, *member, *selfld, *selval, *value, *p, *q;
    char          *goal;
    int            memberok, selok;
    char           buf[512];

    printf("Enter target (ie. goal) names to test them.\n");
    printf("Enter 'fact-name.member=value, ...'\n");
    printf("Enter 'prolog' to drop into an interactive prolog prompt.\n");
    printf("Enter Control-d or quit to exit.\n");

    for (;;) {
        printf("dres> ");

        if (fgets(buf, sizeof(buf), stdin) == NULL)
            break;

        if (!strncmp(buf, "dump ", 5)) {
            for (p=q=buf+5;  *q && *q != '\n';  q++)
                ;
            *q = '\0';

            factmap_vardump(maps, p);
            continue;
        }

        name = member = selfld = selval = value = NULL;
        memberok = selok = FALSE;

        for (p = q = buf;  *p;  p++) {
            if (*p > 0x20 && *p < 0x7f) {
                *q++ = *p;
            }
        }
        *q = 0;

        if (!strcmp(buf, "prolog")) {
            prolog_prompt();
            continue;
        }

        if (!buf[0])
            continue;
        
        if (!strcmp(buf, "quit"))
            break;
        
        if (strchr(buf, '=') != NULL) {
            for (str = buf;   (name = strtok(str, ",")) != NULL;  str = NULL) {
                if ((p = strchr(name, '=')) != NULL) {
                    *p++ = 0;
                    value = p;

                    if ((p = strrchr(name, '.')) != NULL) {
                        *p++ = 0;
                        member = p;

                        if (p[-2] == ']' && (q = strchr(name, '[')) != NULL) {
                            *q = p[-2] = 0;
                            selfld = q + 1;
                            if ((p = strchr(selfld, ':')) == NULL) {
                                printf("Invalid syntax: [%s]\n", selfld);
                                continue;
                            }
                            else {
                                *p++ = 0;
                                selval = p;
                            }
                        }
          
                        for (def = facts;  def->name != NULL;  def++) {
                            if (!strcmp(name, def->name)) {
                                for (m = def->member;  m->name != NULL;  m++) {
                                    if (!strcmp(member, m->name)) {
                                        memberok = TRUE;
                                        if (selfld == NULL) {
                                            selok = TRUE;
                                            break;
                                        }
                                    }
                                    else if (!strcmp(selfld, m->name) &&
                                             !strcmp(selval, m->value)  ) {
                                        selok = TRUE;
                                    }
                                }
                                if (memberok && selok)
                                    break;
                            }
                        }
                        if (def->name == NULL)
                            printf("Can't find %s.%s\n", name, member);
                        else {
                            gval = ohm_value_from_string(value);
                            ohm_fact_set(def->fact, member, &gval);
                            printf("%s:%s = %s\n", name, member, value);
                        }
                    }
                }
            }

#if 0
            factmap_check(maps);
            prolog_prompt();
#endif
            
            printf("updating goal 'all'\n");
            if (dres_update_goal(dres, "all") != 0)
                printf("failed to update goal 'all'\n");
        }
        else {
            goal = buf;
            printf("updating goal '%s'\n", goal);
            if (dres_update_goal(dres, goal) != 0)
                printf("failed to update goal \"%s\"\n", goal);
        }
        
    } /* for ;; */
}


/********************
 * rule_engine_init
 ********************/
static int
rule_engine_init(char *pldir)
{
#define PROLOG_SYSDIR "/usr/share/prolog/"
    char *extensions[] = {
        PROLOG_SYSDIR"extensions/relation",
        PROLOG_SYSDIR"extensions/set",
    };
    int nextension = sizeof(extensions) / sizeof(extensions[0]);

    char *files[] = {
        "hwconfig",
        "devconfig",
        "interface",
        "profile",
        "audio",
#if 0
        "test"
#endif
    };
    int nfile = sizeof(files)/sizeof(files[0]);
    int i;

    char path[PATH_MAX];

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
        snprintf(path, sizeof(path), "%s/%s", pldir, files[i]);
        if (prolog_load_file(path))
            fatal(2, "failed to load %s", path);
    }
    
    return 0;
}


/********************
 * factstore_init
 ********************/
static OhmFactStore *
factstore_init(void)
{
    OhmFactStore  *fs;
    struct member *m;
    GValue         gval;
    int            i;
    
    if ((fs = ohm_fact_store_get_fact_store()) == NULL)
        return NULL;
    
    for (i = 0; facts[i].name != NULL; i++) {
        if ((facts[i].fact = ohm_fact_new(facts[i].name)) == NULL)
            return NULL;
        for (m = facts[i].member; m->name != NULL;  m++) {
            DEBUG("%s:%s=%s", facts[i].name, m->name, m->value);
            gval = ohm_value_from_string(m->value);
            ohm_fact_set(facts[i].fact, m->name, &gval);
        }
        if (!ohm_fact_store_insert(fs, facts[i].fact))
            return NULL;
    }
    
    return fs;
}


/********************
 * filters
 ********************/
int
print_var(int arity, char **row, void*dummy)
{
    int i;

    DEBUG("arity:%d", arity);

    for (i=0; i<arity; i++)
        DEBUG("row[%d]=%s", i, row[i]);
    return 1;
}

int
state(int arity, char **row, void *dummy)
{
#define FIELD_STATE 1
    return (row[FIELD_STATE][0] == '1');        /* hmm, ontology... */
}

int
privacy(int arity, char **row, void *dummy)
{
    return strcmp(row[0], "default") ? 1 : 0;
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

    FIELDS(current_profile, "value");
    FIELDS(connected, "device", "state");
    FIELDS(privacy_override, "value");
    FIELDS(audio_active_policy_group, "group", "state");
    FIELDS(audio_route , "type" , "device");
    FIELDS(volume_limit, "group" , "limit");
    FIELDS(audio_cork, "group", "cork");

    static map_t maps[] = {
        MAP(current_profile ,          F(current_profile)          , NULL   ),
        MAP(connected       ,          F(accessories)              , state  ),
        MAP(privacy_override,          F(privacy_override)         , privacy),
        MAP(audio_active_policy_group, F(audio_active_policy_group), state  ),
        MAP(audio_route     ,          F(audio_route)              , NULL   ),
        MAP(volume_limit    ,          F(volume_limit)             , NULL   ),
        MAP(audio_cork      ,          F(audio_cork)               , NULL   ),
        { .name = NULL }
    };

    map_t  *m;
    
    for (m = maps; m->name; m++) {
        m->fs  = fs;
        m->map = factmap_create(fs, m->name, m->key,m->fields, m->filter,NULL);

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
        if (factmap_update(m->map) != 0) {
            DEBUG("failed!");
            status = 1;
        }
    }

    return status;
}


/*******************
 *
 *******************/
static void
factmap_vardump(map_t *map, char *factname)
{
    OhmFactStore  *fs = ohm_fact_store_get_fact_store();
    GSList        *l;
    GValue        *v;
    OhmFact       *f;
    map_t         *m;
    int            i, j;

    for (m = map; m->name; m++) {
        if (!strcmp(factname, m->name)) {
            if (!(l = ohm_fact_store_get_facts_by_name(fs, m->key)) || 
                g_slist_length(l) == 0) {
                printf("'%s' is in fact-map but no entry in fact-store\n",
                       factname);
            }
            else {
                printf("\n");

                for (i = 0;    l;    i++, l = g_slist_next(l)) {
                    f = l->data;

                    for (j = 0; m->fields[j]; j++) {
                        printf("   %s.%s=", m->key, m->fields[j]);
                        if ((v = ohm_fact_get(f, m->fields[j])) == NULL)
                            printf("<null>\n");
                        else {
                            switch (G_VALUE_TYPE(v)) {

                            case G_TYPE_STRING:
                                printf("'%s'\n", g_value_get_string(v));
                                break;

                            case G_TYPE_INT:
                                printf("%d\n", g_value_get_int(v));
                                break;

                            default:
                                printf("<unknown type %d>\n", G_VALUE_TYPE(v));
                                break;
                            }
                        }
                    }
                    printf("\n");
                }
            }

            return;
        }
    }

    printf("Don't know anything about '%s'\n", factname);
}


/********************
 * prolog_handler
 ********************/
static int
prolog_handler(dres_t *dres, char *name, dres_action_t *action, void **ret)
{
    prolog_predicate_t  *predicates, *p, *pred;
    char                *pred_name, ***objects;
    OhmFact            **facts, *fact;
    char                 buf[64], factname[128];
    int                  status, i;

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
    
    if (!prolog_call(pred, &objects))
        return EINVAL;

    printf("rule engine gave the following objects:\n");
    prolog_dump_objects(objects);

    if (DRES_ID_TYPE(action->lvalue) == DRES_TYPE_FACTVAR) {
        dres_name(dres, action->lvalue, buf, sizeof(buf));
        snprintf(factname, sizeof(factname), "%s.%s", FACT_PREFIX, buf+1);

        status = objects_to_facts(factname, objects, &facts);
        prolog_free_objects(objects);
        
        if (status != 0) {
            printf("failed to convert prolog objects to facts...\n");
            goto fail;
        }
        
        for (i = 0; (fact = facts[i]) != NULL; i++)
            if (!ohm_fact_store_insert(fs, fact))
                printf("##### failed to insert fact %s to fact store\n",
                       factname);
            else
                printf("***** inserted new fact %s\n", factname);
        FREE(facts);
    }

    return 0;

        
 fail:
    if (facts) {
        for (i = 0; (fact = facts[i]) != NULL; i++)
            g_object_unref(fact);
        FREE(facts);
    }

    return status;
}


/********************
 * object_to_fact
 ********************/
static OhmFact *
object_to_fact(char *name, char **object)
{
    OhmFact *fact;
    GValue   value;
    char    *field;
    int      i;

    if (object == NULL || strcmp(object[0], "name") || object[1] == NULL)
        return NULL;
    
    if ((fact = ohm_fact_new(name)) == NULL)
        return NULL;
    
    for (i = 2; object[i] != NULL; i += 2) {
        field = object[i];
        value = ohm_value_from_string(object[i+1]);
        ohm_fact_set(fact, field, &value);
    }
    
    return fact;
}


/********************
 * objects_to_facts
 ********************/
static int
objects_to_facts(char *name, char ***objects, OhmFact ***factptr)
{
    OhmFact **facts = NULL;
    char    **object;
    int       i;
    
    if ((facts = ALLOC_ARR(OhmFact *, 1)) == NULL)
        return ENOMEM;

    for (i = 0; (object = objects[i]) != NULL; i++) {
        if (REALLOC_ARR(facts, i+1, i+2) == NULL)
            return ENOMEM;
        if ((facts[i] = object_to_fact(name, object)) == NULL)
            return EINVAL;
    }
    facts[i] = NULL;

    *factptr = facts;
    return 0;
}


/********************
 * dres_parse_error
 ********************/
void
dres_parse_error(dres_t *dres, int lineno, const char *msg, const char *token)
{
    printf("error: %s, on line %d near input %s\n", msg, lineno, token);
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
