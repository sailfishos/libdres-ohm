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
    char   *in, *out;
    char    compiled[PATH_MAX];
    int     i, verbose;

    
    in = out = NULL;
    verbose  = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o")) {
            if (i >= argc - 1)
                fatal(1, "missing output file name");
            out = argv[++i];
        }
        else if (!strcmp(argv[i], "-v"))
            verbose = 1;
        else {
            if (in != NULL)
                fatal(2, "multiple input files given");
            in = argv[i];
        }
    }

    if (out == NULL) {
        snprintf(compiled, sizeof(compiled), "%sc", in);
        out = compiled;
    }
    else {
        struct stat stin, stout;

        if (stat(out, &stout) == 0) {
            if (stat(in, &stin) != 0)
                fatal(4, "failed to stat input file %s", in);
            if (stin.st_dev == stout.st_dev && stin.st_ino == stout.st_ino)
                fatal(3, "input and output files cannot be the same");
        }
    }
    
    g_type_init();
    
    if (ohm_fact_store_get_fact_store() == NULL)
        fatal(3, "failed to initalize OHM fact store");

    if ((dres = dres_parse_file(in)) == NULL)
        fatal(4, "failed to parse input file %s", in);

    if (dres_finalize(dres))
        fatal(5, "failed to finalize DRES rule file %s", in);

    if (verbose) {
        printf("Targets found in input file %s:\n", in);
        dres_dump_targets(dres);
    }

    unlink(out);

    if (dres_save(dres, out))
        fatal(6, "failed to precompile DRES file %s to %s", in, out);

    dres_exit(dres);

    if ((dres = dres_load(out)) == NULL)
        fatal(7, "failed to load precompiled file %s", out);

    if (verbose) {
        printf("Targets found in compiled file %s:\n", out);
        dres_dump_targets(dres);
    }

#if 0
    dres_exit(dres);
#endif    
    
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
