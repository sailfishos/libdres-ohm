#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <dres/dres.h>
#include <ohm/ohm-fact.h>

#define fatal(ec, fmt, args...) do {                \
        printf("fatal error: " fmt "\n", ## args);  \
        exit(ec);                                   \
    } while (0)


int
main(int argc, char *argv[])
{
    dres_t *dres;
    char   *in = argc >= 2 ? argv[1] : "test.dres";
    int     i;

    g_type_init();
    
    if (ohm_fact_store_get_fact_store() == NULL)
        fatal(3, "failed to initalize OHM fact store");

    if ((dres = dres_parse_file(in)) == NULL)
        fatal(4, "failed to parse input file %s", in);

    if (dres_finalize(dres))
        fatal(5, "failed to finalize DRES rule file %s", in);

    printf("Targets found in input file %s:\n", in);
    dres_dump_targets(dres);

    for (i = 2; i < argc; i++) {
      printf("Testing: dres_update_goal(%s)...\n", argv[i]);
      dres_update_goal(dres, argv[i], NULL);
    }

    dres_exit(dres);
    
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
