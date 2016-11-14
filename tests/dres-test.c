/*************************************************************************
This file is part of dres the resource policy dependency resolver.

Copyright (C) 2010 Nokia Corporation.

This library is free software; you can redistribute
it and/or modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation
version 2.1 of the License.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
USA.
*************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>

#include <dres/dres.h>
#include <ohm/ohm-fact.h>

#define DEFAULT_RULESET "./ruleset.dres"

#define fatal(ec, fmt, args...) do {                \
        printf("fatal error: " fmt "\n", ## args);  \
        exit(ec);                                   \
    } while (0)


static const char   *ruleset;
static dres_t       *dres;
static OhmFactStore *store;
static int           quit = 0;

typedef void (*handler_t)(char *command, char *args);

static void resolve_handler(char *, char *);
static void show_handler(char *, char *);
static void help_handler(char *, char *);
static void quit_handler(char *, char *);


typedef struct {
    char      *command;
    handler_t  handler;
} command_t;


static command_t commands[] = {
    { "resolve", resolve_handler },
    { "show"   , show_handler    },
    { "dump"   , show_handler    },
#if 0
    { "trace"  , trace_handler   },
#endif
    { "help"   , help_handler    },
    { "quit"   , quit_handler    },
    { NULL, NULL }
};


/********************
 * factstore_init
 ********************/
void
factstore_init(void)
{
    if ((store = ohm_get_fact_store()) == NULL)
        fatal(1, "Failed to initialize factstore.");
}


/********************
 * factstore_exit
 ********************/
void
factstore_exit(void)
{
    if (store) {
        g_object_unref(store);
        store = NULL;
    }
}


/********************
 * resolver_init
 ********************/
void
resolver_init(const char *ruleset)
{
    if ((dres = dres_open((char *)ruleset)) == NULL)
        fatal(1, "Failed to initialize resolver with '%s'.", ruleset);
    
    if (dres_finalize(dres) != 0)
        fatal(1, "Failed to finalize resolver.");
}


/********************
 * resolver_exit
 ********************/
void
resolver_exit(void)
{
    if (dres) {
        dres_exit(dres);
        dres = NULL;
    }
}


/********************
 * handler_lookup
 ********************/
static handler_t
handler_lookup(const char *command)
{
    command_t *c;

    for (c = commands; c->command != NULL; c++)
        if (!strcmp(c->command, command))
            return c->handler;

    return NULL;
}


/********************
 * help_handler
 ********************/
static void
help_handler(char *command, char *args)
{
    (void)command;
    
    if (args == NULL || !*args) {
        printf("Possible commands are:\n");
        printf("  resolve goal  resolve \n");
        printf("  show [name]   show all or a given fact\n");
        printf("  help          minimal help on usage\n");
        printf("  quit          clean up and exit\n");
    }
}


/********************
 * quit_handler
 ********************/
static void
quit_handler(char *command, char *args)
{
    (void)command;
    (void)args;
    
    quit = 1;
}


/********************
 * resolve_handler
 ********************/
static void
resolve_handler(char *command, char *args)
{
    char *goal, *end;
    int   status;

    (void)command;
        
    goal = args;
    end  = strchr(goal, ' ');

    if (end != NULL)
        *end = '\0';

    /* XXX parse and construct argument list */

    status = dres_update_goal(dres, goal, NULL);

    if (status < 0)
        printf("Updating goal '%s' failed with and error.\n", goal);
    else if (!status)
        printf("Failed to update goal '%s.'\n", goal);
    else
        printf("Goal '%s' successfully updated.\n", goal);
}


/********************
 * show_handler
 ********************/
void
show_handler(char *command, char *args)
{
    char     name[128], *s, *e, *fstr;
    size_t   len;
    GSList  *l;
    OhmFact *fact;

    (void)command;
    
    if (args == NULL || !*args) {
        fstr = ohm_fact_store_to_string(store);
        printf("fact store contents: %s\n", fstr);
        g_free(fstr);
        
        return;
    }
    
    s = args;
    while (s && *s) {
        if ((e = strchr(s, ' ')) != NULL) {
        delimited:
            if ((len = (int)e - (int)s + 1) > sizeof(name) - 1)
                len = sizeof(name) - 1;
            strncpy(name, s, len);
            name[len] = '\0';
            e = s + 1;
        }
        else if ((e = strchr(s, ',')) != NULL)
            goto delimited;
        else {
            len = sizeof(name) - 1;
            strncpy(name, s, len);
            name[len] = '\0';
        }
        
        if ((s = e) != NULL)
            while (*s == ' ')
                s++;

        l = ohm_fact_store_get_facts_by_name(store, name);
        while (l != NULL) {
            fact = (OhmFact *)l->data;

            fstr = ohm_structure_to_string(OHM_STRUCTURE(fact));
            printf("%s\n", fstr ? fstr : "");
            g_free(fstr);

            l = g_slist_next(l);
        }
    }
}


/********************
 * command_execute
 ********************/
void
command_execute(char *cmdstr)
{
    char      input[1024], *end, *command, *args;
    handler_t handler;
    
    strncpy(input, cmdstr, sizeof(input) - 1);
    input[sizeof(input) - 1] = '\0';
    
    if ((end = strrchr(input, '\n')) != NULL)
        *end = '\0';

    if (!input[0])
        return;
    
    command = input;
    end     = strchr(command, ' ');
        
    if (end != NULL) {
        *end = '\0';
        args = end + 1;
        while (*args == ' ' || *args == '\t')
            args++;
    }
    else
        args = NULL;
        
    if ((handler = handler_lookup(command)) == NULL) {
        printf("Unknown command '%s'.\n", command);
        return;
    }
    
    handler(command, args);
}


/********************
 * startup
 ********************/
static void
startup(void)
{
#if (GLIB_MAJOR_VERSION <= 2) && (GLIB_MINOR_VERSION < 36)
    g_type_init();
#endif
    factstore_init();
    resolver_init(ruleset);
    
    dres_dump_targets(dres);
    printf("=========================================\n");
}


/********************
 * shutdown
 ********************/
static void
shutdown(void)
{
    resolver_exit();
    factstore_exit();

    exit(0);
}


int
main(int argc, char *argv[])
{
    char input[1024], *command;
    int  a, i, loops;

    if (argc < 2 || (argv[1][0] == '-' && argv[1][1] == '\0'))
        ruleset = DEFAULT_RULESET;
    else
        ruleset = argv[1];
    
    startup();
    
    a = 2;
    while (a < argc - 1) {
        command = argv[a++];
        loops   = (int)strtol(argv[a++], NULL, 10);
        printf("Running %d iterations of '%s'\n", loops, command);

        for (i = 0; i < loops; i++)
            command_execute(command);
    }
    
    if (quit)
        shutdown();
        
#define PROMPT(s) do { printf("%s ", s); fflush(stdout); } while (0)
    PROMPT(">");
    while (!quit && fgets(input, sizeof(input), stdin) != NULL) {
        command_execute(input);
        PROMPT(">");
    }
    
    shutdown();

    return 0;
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




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
