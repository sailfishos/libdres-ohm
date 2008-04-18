#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "dres.h"
#include "parser.h"

#define fatal(ec, fmt, args...) do {                \
        printf("fatal error: " fmt "\n", ## args);  \
        exit(ec);                                   \
    } while (0)


int
main(int argc, char *argv[])
{
    char *rulefile;
    char *goal;
    int   status;

    rulefile = argc < 2 ? NULL  : argv[1];
    goal     = argc < 3 ? "all" : argv[2];

    if ((status = dres_init(rulefile)) != 0)
        fatal(status, "failed to initialize dres with \"%s\"", rulefile);

    dres_dump_targets();
    
    if ((status = dres_update_goal(goal)) != 0)
        printf("failed to update goal \"%s\"\n", goal);
    
    dres_exit();
    
    return 0;
}


void
yyerror(const char *msg)
{
  printf("parse error: %s (%s)\n", msg, yylval.string);
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
