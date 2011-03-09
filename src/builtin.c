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
#include <errno.h>
#include <regex.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <ohm/ohm-fact.h>
#include <dres/dres.h>
#include <dres/compiler.h>
#include "dres-debug.h"


#define BUILTIN_HANDLER(b)                                              \
    static int dres_builtin_##b(void *data, char *name,                 \
                                vm_stack_entry_t *args, int narg,       \
                                vm_stack_entry_t *rv)

BUILTIN_HANDLER(dres);
BUILTIN_HANDLER(resolve);
BUILTIN_HANDLER(echo);
BUILTIN_HANDLER(fact);
BUILTIN_HANDLER(shell);
BUILTIN_HANDLER(regexp_read);
BUILTIN_HANDLER(fail);

#define BUILTIN(b) { .name = #b, .handler = dres_builtin_##b }

typedef struct dres_builtin_s {
    char           *name;
    dres_handler_t  handler;
} dres_builtin_t;

static dres_builtin_t builtins[] = {
    BUILTIN(dres),
    BUILTIN(resolve),
    BUILTIN(echo),
    BUILTIN(fact),
    BUILTIN(shell),
    BUILTIN(regexp_read),
    BUILTIN(fail),
    { .name = NULL, .handler = NULL }
};


/*****************************************************************************
 *                            *** builtin handlers ***                       *
 *****************************************************************************/

/********************
 * dres_fallback_call
 ********************/
int
dres_fallback_call(void *data, char *name,
                   vm_stack_entry_t *args, int narg, vm_stack_entry_t *rv)
{
    dres_t *dres = (dres_t *)data;

    if (dres->fallback) 
        return dres->fallback(data, name, args, narg, rv);
    else {
        DEBUG(DBG_RESOLVE, "unknown action %s", name);
        /* XXX TODO: dump arguments */
        DRES_ACTION_ERROR(EINVAL);
    }
    
}


/********************
 * dres_fallback_handler
 ********************/
EXPORTED dres_handler_t
dres_fallback_handler(dres_t *dres, dres_handler_t handler)
{
    dres_handler_t old;

    old            = dres->fallback;
    dres->fallback = handler;

    return old;
}


/********************
 * dres_register_builtins
 ********************/
int
dres_register_builtins(dres_t *dres)
{
    dres_builtin_t *b;
    int             status;
    void           *data;

    for (b = builtins; b->name; b++)
        if ((status = dres_register_handler(dres, b->name, b->handler)) != 0)
            return status;
    
    data = dres;
    vm_method_default(&dres->vm, dres_fallback_call, &data);
    
    return 0;
}


/********************
 * dres_builtin_dres
 ********************/
BUILTIN_HANDLER(dres)
{
    dres_t       *dres = (dres_t *)data;
    char         *goal;
    vm_chunk_t   *chunk;
    unsigned int *pc;
    int           ninstr;
    int           nsize;
    int           status;
    const char   *info;

    (void)name;

    if (narg < 1)
        goal = NULL;
    else {
        if (args[0].type != DRES_TYPE_STRING)
            DRES_ACTION_ERROR(EINVAL);
        goal = args[0].v.s;
    }
    
    /* save VM context */
    chunk  = dres->vm.chunk;
    pc     = dres->vm.pc;
    ninstr = dres->vm.ninstr;
    nsize  = dres->vm.nsize;
    info   = dres->vm.info;

    DEBUG(DBG_RESOLVE, "recursively resolving %sgoal %s",
          goal ? "" : "the default ", goal ? goal : "");
    
    status = dres_update_goal(dres, goal, NULL);

    DEBUG(DBG_RESOLVE, "resolved %sgoal %s with status %d (%s)",
          goal ? "" : "the default ", goal ? goal : "", status,
          status < 0 ? "error" : (status ? "success" : "failure"));
    
    /* restore VM context */
    dres->vm.chunk  = chunk;
    dres->vm.pc     = pc;
    dres->vm.ninstr = ninstr;
    dres->vm.nsize  = nsize;
    dres->vm.info   = info;

    rv->type = DRES_TYPE_INTEGER;
    rv->v.i  = status;

    return status;
}


/********************
 * dres_builtin_resolve
 ********************/
BUILTIN_HANDLER(resolve)
{
    return dres_builtin_dres(data, name, args, narg, rv);
}


static FILE *
redirect(char *path, FILE *current)
{
    const char *mode;
    FILE       *fp;

    if (path[0] == '>' && path[1] == '>') {
        path += 2;
        mode  = "a";
    }
    else {
        path++;
        mode = "w";
    }

    if (!strcmp(path, "stdout"))
        fp = stdout;
    else if (!strcmp(path, "stderr"))
        fp = stderr;
    else
        fp = fopen(path, mode);
    
    if (fp == NULL)
        fp = current;
    else
        if (current != stdout && current != stderr)
            fclose(current);
    
    return fp;
}


/********************
 * dres_builtin_echo
 ********************/
BUILTIN_HANDLER(echo)
{
    dres_t *dres = (dres_t *)data;
    FILE   *fp;
    char   *t;
    int     i;

    (void)dres;
    (void)name;
    
    fp = stdout;

    t = "";
    for (i = 0; i < narg; i++) {
        switch (args[i].type) {
        case DRES_TYPE_STRING:
            if (args[i].v.s[0] == '>') {
                fp = redirect(args[i].v.s, fp);
                t = "";
                continue;
            }
            else
                fprintf(fp, "%s%s", t, args[i].v.s);
            break;
            
        case DRES_TYPE_NIL:     fprintf(fp, "%s<nil>", t);           break;
        case DRES_TYPE_INTEGER: fprintf(fp, "%s%d", t, args[i].v.i); break;
        case DRES_TYPE_DOUBLE:  fprintf(fp, "%s%f", t, args[i].v.d); break;
        case DRES_TYPE_FACTVAR:
            fprintf(fp, "%s", t);
            vm_global_print(fp, args[i].v.g);
            break;
        default:
            fprintf(fp, "<unknown>");
        }
        t = " ";
    }
    
    fprintf(fp, "\n");
    fflush(fp);
    
    if (fp != stdout && fp != stderr)
        fclose(fp);

    rv->type = DRES_TYPE_INTEGER;
    rv->v.i  = 0;
    DRES_ACTION_SUCCEED;
}


/********************
 * dres_builtin_fact
 ********************/
BUILTIN_HANDLER(fact)
{
    dres_t       *dres = (dres_t *)data;
    vm_global_t  *g;
    GValue       *value;
    char         *field, *factname;
    int           a, i, err;

    (void)dres;
    (void)name;

    g = NULL;

    if (narg < 1) {
        DRES_ERROR("builtin 'fact': called with no arguments");
        DRES_ACTION_ERROR(EINVAL);
    }

    if (args[0].type != DRES_TYPE_STRING) {
        DRES_ERROR("builtin 'fact': invalid fact name (type 0x%x)",
                   args[i].type);
        err = EINVAL;
        goto fail;
    }

    factname = args[0].v.s;
    
    if ((g = vm_global_alloc(narg - 1)) == NULL) {
        DRES_ERROR("builtin 'fact': failed to allocate new global");
        DRES_ACTION_ERROR(ENOMEM);
    }
    
    for (a = 0, i = 1; a < narg && i < narg; a++) {
        printf("* outer: i=%d, a=%d\n", i, a);
        g->facts[a] = ohm_fact_new(factname);
        g->nfact = a + 1;
        printf("* created fact %d...\n", a + 1);
        while (i < narg) {
            printf("* inner: i=%d, a=%d\n", i, a);
            if (args[i].type != DRES_TYPE_STRING) {
                DRES_ERROR("builtin 'fact': invalid field name (type 0x%x)",
                           args[i].type);
                err = EINVAL;
                goto fail;
            }

            field = args[i].v.s;
            if (!field[0]) {
                i++;
                break;
            }
            
            if (i == narg - 1) {
                DRES_ERROR("builtin 'fact': missing value for field %s", field);
                err = EINVAL;
                goto fail;
            }

            i++;
            
            switch (args[i].type) {
            case DRES_TYPE_INTEGER:
                value = ohm_value_from_int(args[i].v.i);
                break;
            case DRES_TYPE_STRING:
                value = ohm_value_from_string(args[i].v.s);
                break;
            case DRES_TYPE_DOUBLE:
                value = ohm_value_from_double(args[i].v.d);
                break;
            default:
                DRES_ERROR("builtin 'fact': invalid value for field %s", field);
                err = EINVAL;
                goto fail;
            }
            
            ohm_fact_set(g->facts[a], field, value);
            
            i++;
        }
    }
    
    rv->type = DRES_TYPE_FACTVAR;
    rv->v.g  = g;
    DRES_ACTION_SUCCEED;

 fail:
    if (g)
        vm_global_free(g);
    
    DRES_ACTION_ERROR(err);    
}


/********************
 * dres_builtin_shell
 ********************/
BUILTIN_HANDLER(shell)
{
    int status;

    if (narg != 1) {
        DRES_ERROR("builtin 'shell': passed %d arguments instead of 1", narg);
        DRES_ACTION_ERROR(EINVAL);
    }

    if (args[0].type != DRES_TYPE_STRING) {
        DRES_ERROR("builtin 'shell': argument must be of type string");
        DRES_ACTION_ERROR(EINVAL);
    }

    errno  = 0;
    status = system(args[0].v.s);
    
    if (status < 0) {
        if (errno)
            DRES_ACTION_ERROR(errno);
        else
            DRES_ACTION_ERROR(EINVAL);
    }
    
    status = WEXITSTATUS(status);

    if (status == 0) {
        rv->type = DRES_TYPE_INTEGER;
        rv->v.i  = 0;
        DRES_ACTION_SUCCEED;
    }
    else
        DRES_ACTION_ERROR(status);
}


/********************
 * dres_builtin_regexp_read
 ********************/
BUILTIN_HANDLER(regexp_read)
{
    /*
     * Usage: regexp_read(path, regexp, nth, type, default)
     *
     * This builtin will open <path>, read it and match it line-by-line
     * against the given <regexp>, until a match is found. The <nth>
     * matching substring will be returned converted to a type denoted
     * by <type> ('i', 'd', 's'). If <default> is not omitted and it is
     * type-compatible with <type>, <default> will be returned upon any
     * error. Otherwise an exception will be thrown. Substring match
     * indices start from 0 but keep in mind that the 0th index is always
     * the full match.
     *
     * You are limited to 1K long lines and 31 substring matches.
     *
     * Example:
     *
     * regexp_read('/proc/1/status', \
     *             '(VmRSS:)[[:space:]]*([0-9]+)', 1, 'i', '-1')
     *
     * should return the RSS size in kilobytes of process 1 (usually
     * /sbin/init) as a DRES_TYPE_INTEGER on the top of the stack. Upon
     * error it will not fail, returning -1 instead.
     *
     * You do not want to use this builtin unless you really understand
     * how substring matches work in POSIX regex(3)ps.
     */

    const char       *path, *expr, *type;
    int               nth;
    vm_stack_entry_t *defval;
    FILE             *fp;
    char              buf[1024], match[1024], *end;
    regex_t           rebuf, *re;
    regmatch_t        rm[32];
    int               max, len;
    
    
    defval = NULL;
    fp     = NULL;
    re     = NULL;
    max    = sizeof(rm) / sizeof(rm[0]);

    if (narg == 4 || narg == 5) {
        if (args[0].type != DRES_TYPE_STRING  ||
            args[1].type != DRES_TYPE_STRING  ||
            args[2].type != DRES_TYPE_INTEGER ||
            args[3].type != DRES_TYPE_STRING) {
            DRES_ERROR("args of incorrect type to builtin 'regexp_read'");
            DRES_ACTION_ERROR(EINVAL);
        }
        
        path   = args[0].v.s;
        expr   = args[1].v.s;
        nth    = args[2].v.i;
        type   = args[3].v.s;
        defval = (narg == 5 ? args + 4 : NULL);
    }
    else {
        DRES_ERROR("builtin 'regexp_read' needs "
                   " (path, prefix, regexp, type [,defval]) arguments");
        DRES_ACTION_ERROR(EINVAL);
    }
    
    if (!type[0] || type[1] ||
        (type[0] != 'i' && type[0] != 'd' && type[0] != 's')) {
        DRES_ERROR("invalid type argument to builtin 'regexp_read'");
        DRES_ACTION_ERROR(EINVAL);
    }

    if (nth < 0 || nth >= 32) {
        DRES_ERROR("invalid substring index (%d) for builtin "
                   "'regexp_read' (allowed: 0 - %d)", nth, max - 1);
        DRES_ACTION_ERROR(EOVERFLOW);
    }

    if (defval != NULL) {
        if ((type[0] == 'i' && defval->type != DRES_TYPE_INTEGER) ||
            (type[0] == 'd' && defval->type != DRES_TYPE_DOUBLE ) ||
            (type[0] == 's' && defval->type != DRES_TYPE_STRING )) {
            DRES_WARNING("default inconsistent with type string in "
                         "builtin 'regexp_read'");
            defval = NULL;
        }
    }
    
    if (regcomp(&rebuf, expr, REG_EXTENDED) != 0) {
        DRES_ERROR("invalid regular expression '%s' passed to "
                   "builtin 'regexp_read'", expr);
        DRES_ACTION_ERROR(EINVAL);
    }

    re = &rebuf;


    if ((fp = fopen(path, "r")) == NULL)
        goto error;

    while (fgets(buf, sizeof(buf), fp) != NULL) {
        if (regexec(re, buf, max, rm, 0) != 0)     /* line does not match */
            continue;                              
        if (rm[nth].rm_so == -1)                   /* no nth match */
            continue;

        len = rm[nth].rm_eo - rm[nth].rm_so;
        if (len >= sizeof(match)) {
            DRES_WARNING("too long match string in builtin 'regex_read'");
            goto error;
        }

        strncpy(match, buf + rm[nth].rm_so, len);
        match[len] = '\0';
        
        switch (type[0]) {
        case 'i':
            rv->type = DRES_TYPE_INTEGER;
            rv->v.i  = (int)strtol(match, &end, 10);
            if (end && *end) {
                DRES_WARNING("match '%s' in builtin 'regex_read' is not "
                             "a valid integer", match);
                goto error;
            }
            else
                goto success;
            break;
            
        case 'd':
            rv->type = DRES_TYPE_DOUBLE;
            rv->v.d  = strtod(match, &end);
            if (end && *end) {
                DRES_WARNING("match '%s' in builtin 'regex_read' is not "
                             "a valid double", match);
                goto error;
            }
            else
                goto success;
            break;

        case 's':
            DRES_ERROR("currently you cannot use builtin 'regexp_read' to "
                       "match strings, sorry...");
            goto error;
            break;
        }
    }
    

 success:
    if (fp != NULL)
        fclose(fp);
    
    if (re)
        regfree(re);

    DRES_ACTION_SUCCEED;
    /* not reached */
    
 error:
    if (fp != NULL)
        fclose(fp);
    
    if (re)
        regfree(re);
    
    /*
     * Notes: By convention, we only fail if no default was given, or if the
     *        given default was not type-compatible with the type string.
     */
    
    if (defval != NULL) {
        rv->type = defval->type;
        rv->v    = defval->v;
        DRES_ACTION_SUCCEED;
    }
    else
        DRES_ACTION_ERROR(EINVAL);
}


/********************
 * dres_builtin_fail
 ********************/
BUILTIN_HANDLER(fail)
{
    int err;

    (void)data;
    (void)name;
    
    if (narg > 0 && args[0].type == DRES_TYPE_INTEGER)
        err = args[0].v.i;
    else
        err = EINVAL;
    
    rv->type = DRES_TYPE_UNKNOWN;
    DRES_ACTION_ERROR(err);
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
