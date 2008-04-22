#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "dres.h"
#include "parser.h"

#include "vala/ohm-fact.h"

#define fatal(ec, fmt, args...) do {                \
        printf("fatal error: " fmt "\n", ## args);  \
        exit(ec);                                   \
    } while (0)

extern int lexer_lineno(void);

int
main(int argc, char *argv[])
{
#define FACT_PREFIX "com.nokia.policy."
#define F(n) FACT_PREFIX#n

    OhmFactStore *fs;
    GValue        gval;

    char *rulefile;
    char *goal;
    int   status;

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
        { F(temperature), NULL },
        { F(current_profile), NULL },
        { F(privacy_override), NULL },
        { F(connected), NULL },
        { F(audio_active_policy_group), NULL },
        { F(volume_limit), NULL },
        { F(audio_cork), NULL },
        { F(cpu_load), NULL },
        { F(audio_playback_request), NULL },
        { F(audio_playback), NULL },
        { F(current_profile), NULL },
        { F(connected), NULL },
        { NULL, NULL }
    };
    int i;

    rulefile = argc < 2 ? NULL  : argv[1];
    goal     = argc < 3 ? "all" : argv[2];

    g_type_init();
    
    if ((fs = ohm_fact_store_get_fact_store()) == NULL)
        fatal(1, "could not get/create fact store");

    for (i = 0; facts[i].name != NULL; i++) {
        if ((facts[i].fact = ohm_fact_new(facts[i].name)) == NULL)
            fatal(1, "could not create fact %s", facts[i].name);
        gval = ohm_value_from_string("bar");
        ohm_fact_set(facts[i].fact, "foo", &gval);
        if (!ohm_fact_store_insert(fs, facts[i].fact))
            fatal(1, "failed to insert fact %s to fact store", facts[i].name);
    }
    
    if ((status = dres_init(rulefile)) != 0)
        fatal(status, "failed to initialize dres with \"%s\"", rulefile);

    dres_dump_targets();
    
    if ((status = dres_update_goal(goal)) != 0)
        printf("failed to update goal \"%s\"\n", goal);

    printf("###############################################\n");
    

    for (i = 0; facts[i].name != NULL; i++) {
        OhmFact *fact;
        
        if (strcmp(facts[i].name, F(temperature)))
            continue;
        
        if ((fact = ohm_fact_new(facts[i].name)) == NULL)
            fatal(1, "could not create fact %s", facts[i].name);
        gval = ohm_value_from_string("barfoo");
        ohm_fact_set(fact, "foobar", &gval);
        if (!ohm_fact_store_insert(fs, fact))
            fatal(1, "failed to insert fact %s to fact store", facts[i].name);
    }

#ifdef FIELD_ADDITIONS_ARE_IN_CHANGESETS
    for (i = 0; facts[i].name != NULL; i++) {
        if (strcmp(facts[i].name, F(temperature)))
            continue;
        gval = ohm_value_from_string("foobar");
        ohm_fact_set(facts[i].fact, "barfoo", &gval);
        printf("***** mutated variable %s *****\n", facts[i].name);
    }
#endif    

    if ((status = dres_update_goal(goal)) != 0)
        printf("failed to update goal \"%s\"\n", goal);
    
    dres_exit();
    
    return 0;
}


void
yyerror(const char *msg)
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
