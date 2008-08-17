#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>

#include <ohm/ohm-fact.h>

#include <dres/dres.h>
#include <dres/compiler.h>

#include "dres-debug.h"
#include "parser-types.h"
#include "parser.h"


/* trace flags */
int DBG_GRAPH, DBG_VAR, DBG_RESOLVE;

TRACE_DECLARE_COMPONENT(trcdres, "dres",
    TRACE_FLAG_INIT("graph"  , "dependency graph"    , &DBG_GRAPH),
    TRACE_FLAG_INIT("var"    , "variable handling"   , &DBG_VAR),
    TRACE_FLAG_INIT("resolve", "dependency resolving", &DBG_RESOLVE));
    

extern int   lexer_open(char *path);
extern int   lexer_lineno(void);
extern int   yyparse(dres_t *dres);

int  initialize_variables(dres_t *dres);
int  finalize_variables  (dres_t *dres);
static int  finalize_actions    (dres_t *dres);

static int  push_locals(dres_t *dres, char **locals);
static int  pop_locals (dres_t *dres);

#if 0
static int  save_initializers(dres_t *dres, dres_t *copy,
                              dres_buf_t *buf, dres_buf_t *strbuf);
static int  load_initializers(dres_t *dres, ptrdiff_t diff, ptrdiff_t strdiff);

static int  save_methods(dres_t *dres, dres_t *copy,
                         dres_buf_t *buf, dres_buf_t *strbuf);
static int  load_methods(dres_t *dres, ptrdiff_t diff, ptrdiff_t strdiff);
#endif

int depth = 0;


/********************
 * dres_init
 ********************/
EXPORTED dres_t *
dres_init(char *prefix)
{
    dres_t *dres;
    int     status;
    
    trace_init();
    trace_add_component(NULL, &trcdres);
    
    if (ALLOC_OBJ(dres) == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    if (vm_init(&dres->vm, 32))
        goto fail;

    if (dres_store_init(dres))
        goto fail;

    if ((status = dres_register_builtins(dres)) != 0)
        goto fail;

    dres->stamp = 1;

    if (prefix != NULL && prefix[0] != '\0')
        printf("*** WARNING: ignoring deprecated DRES prefix \"%s\"\n", prefix);
    
    return dres;
    
 fail:
    dres_dump_targets(dres);
    dres_exit(dres);
    return NULL;
}


/********************
 * dres_exit
 ********************/
EXPORTED void
dres_exit(dres_t *dres)
{
    if (dres == NULL)
        return;
    
    dres_store_free(dres);

    if (!DRES_TST_FLAG(dres, COMPILED))
        free(dres);
    else {
        dres_free_targets(dres);
        dres_free_factvars(dres);
        dres_free_dresvars(dres);
        FREE(dres);
    }
}


/********************
 * dres_parse_file
 ********************/
EXPORTED int
dres_parse_file(dres_t *dres, char *path)
{
    int status;
    
    if (path == NULL)
        return EINVAL;
    
    if ((status = lexer_open(path)) != 0)
        return status;
    
    status = yyparse(dres);
    
#if 0
    dres_dump_targets(dres);
#endif

    if (status == 0)
        status = initialize_variables(dres);
    if (status == 0)
        status = finalize_variables(dres);

    dres->vm.nlocal = dres->ndresvar;
    
    return status;
}


/********************
 * create_variable
 ********************/
static int
create_variable(dres_t *dres, char *name, dres_init_t *fields)
{
    dres_init_t  *init;
    char         *field;
    dres_value_t *value;
    OhmFactStore *store = ohm_get_fact_store();
    OhmFact      *fact;
    GValue       *gval;

    if (store == NULL)
        return EINVAL;

    if ((fact = ohm_fact_new(name)) == NULL)
        return ENOMEM;

    for (init = fields; init != NULL; init = init->next) {
        field = init->field.name;
        value = &init->field.value;
        switch (value->type) {
        case DRES_TYPE_INTEGER: gval = ohm_value_from_int(value->v.i);    break;
        case DRES_TYPE_DOUBLE:  gval = ohm_value_from_double(value->v.d); break;
        case DRES_TYPE_STRING:  gval = ohm_value_from_string(value->v.s); break;
        default:                return EINVAL;
        }

        ohm_fact_set(fact, field, gval);
    }

    if (!ohm_fact_store_insert(store, fact))
        return EINVAL;

    return 0;

    (void)dres;
}


/********************
 * initialize_variables
 ********************/
int
initialize_variables(dres_t *dres)
{
    dres_initializer_t *init;
    char                name[128];
    int                 status;

    for (init = dres->initializers; init != NULL; init = init->next) {
        dres_name(dres, init->variable, name, sizeof(name));
        if ((status = create_variable(dres, name + 1, init->fields)) != 0)
            return status;
    }
    
    return 0;
}


/********************
 * finalize_variables
 ********************/
int
finalize_variables(dres_t *dres)
{
    return dres_store_track(dres);
}


/********************
 * finalize_actions
 ********************/
static int
finalize_actions(dres_t *dres)
{
    dres_target_t  *target;
    dres_action_t  *action;
    dres_call_t    *call;
    void           *unknown = dres_lookup_handler(dres, DRES_BUILTIN_UNKNOWN);
    int             i, status;
    
    status = 0;
    for (i = 0, target = dres->targets; i < dres->ntarget; i++, target++) {
        for (action = target->actions; action; action = action->next) {
            if (action->type != DRES_ACTION_CALL)
                continue;
            call = action->call;
            if (!(call->handler = dres_lookup_handler(dres, call->name))) {
                call->handler = unknown;
                status = ENOENT;
            }
        }
        if ((status = dres_compile_target(dres, target)) != 0)
            return status;
    }

    printf("*** succefully compiled all targets\n");

    DRES_SET_FLAG(dres, ACTIONS_FINALIZED);
    return 0;
}


/********************
 * finalize_targets
 ********************/
static int
finalize_targets(dres_t *dres)
{
    dres_target_t *target;
    dres_graph_t  *graph;
    char           goal[64];
    int            i;

    for (i = 0, target = dres->targets; i < dres->ntarget; i++, target++) {
        dres_name(dres, target->id, goal, sizeof(goal));

        if ((graph = dres_build_graph(dres, target)) == NULL)
            return EINVAL;
        
        target->dependencies = dres_sort_graph(dres, graph);
        dres_free_graph(graph);

        if (target->dependencies == NULL)
            return EINVAL;

        DEBUG(DBG_GRAPH, "topological sort for goal %s:\n", goal);
        dres_dump_sort(dres, target->dependencies);
    }

    DRES_SET_FLAG(dres, TARGETS_FINALIZED);
    return 0;
}


/********************
 * dres_finalize
 ********************/
EXPORTED int
dres_finalize(dres_t *dres)
{
    int status;
    
    if ((status = finalize_actions(dres)) || (status = finalize_targets(dres)))
        return status;
    else
        return 0;
}


/********************
 * dres_update_goal
 ********************/
EXPORTED int
dres_update_goal(dres_t *dres, char *goal, char **locals)
{
    dres_target_t *target;
    int            id, i, status, own_tx;

    
    status = 0;

    if (!DRES_TST_FLAG(dres, ACTIONS_FINALIZED))
        if ((status = finalize_actions(dres)) != 0)
            if (dres->fallback == NULL)
                return status;
    
    if (!DRES_TST_FLAG(dres, TARGETS_FINALIZED))
        if ((status = finalize_targets(dres)) != 0)
            return status;
    
    if (goal != NULL) {
        if ((target = dres_lookup_target(dres, goal)) == NULL)
            return EINVAL;
    }
    else {
        target = dres->targets;
        goal   = target->name;
    }
    
    if (!DRES_IS_DEFINED(target->id))
        return EINVAL;

    if (!DRES_TST_FLAG(dres, TRANSACTION_ACTIVE)) {
        if (!dres_store_tx_new(dres))
            return EINVAL;

        dres->txid++;
        own_tx = 1;
    }

    dres->stamp++;
    dres_store_check(dres);
    
    if (locals != NULL && (status = push_locals(dres, locals)) != 0)
        goto rollback;
    
    if (target->prereqs == NULL) {
        DEBUG(DBG_RESOLVE, "%s has no prereqs => updating", target->name);
        status = dres_run_actions(dres, target);
    }
    else {
        for (i = 0; target->dependencies[i] != DRES_ID_NONE; i++) {
            id = target->dependencies[i];
        
            if (DRES_ID_TYPE(id) != DRES_TYPE_TARGET)
                continue;
        
            if ((status = dres_check_target(dres, id)) != 0)
                break;
        }
    }
    
    if (locals != NULL)
        pop_locals(dres);
    
    if (status == 0) {
        dres_update_target_stamp(dres, target);
        if (own_tx)
            dres_store_tx_commit(dres);
    }
    else {
    rollback:
        if (own_tx)
            dres_store_tx_rollback(dres);
    }
    
    return status;
}


/********************
 * dres_lookup_variable
 ********************/
EXPORTED dres_variable_t *
dres_lookup_variable(dres_t *dres, int id)
{
    int idx = DRES_INDEX(id);
    
    switch (DRES_ID_TYPE(id)) {
    case DRES_TYPE_FACTVAR:
        return idx > dres->nfactvar ? NULL : dres->factvars + idx;
    case DRES_TYPE_DRESVAR:
        return idx > dres->ndresvar ? NULL : dres->dresvars + idx;
    }        

    return NULL;
}


/********************
 * push_locals
 ********************/
static int
push_locals(dres_t *dres, char **locals)
{
#define FAIL(ec) do { err = (ec); goto fail; } while (0)
    vm_value_t v;
    int        err, id, i;
    
    if (locals == NULL)
        return 0;
    
    if ((err = vm_scope_push(&dres->vm)) != 0)
        return err;
    
    for (i = 0; locals[i] != NULL; i += 2) {
        id  = dres_dresvar_id(dres, locals[i]);
        v.s = locals[i+1];

        if (v.s == NULL)
            FAIL(EINVAL);
            
        if (id == DRES_ID_NONE) {
            printf("*** cannot set unknown &%s to \"%s\"\n", locals[i], v.s);
            FAIL(ENOENT);
        }
            
        if ((err = vm_scope_set(dres->vm.scope, id, DRES_TYPE_STRING, v)) != 0)
            FAIL(err);
    }
    
    
    return 0;

 fail:
    vm_scope_pop(&dres->vm);
    return err;
}


/********************
 * pop_locals
 ********************/
static int
pop_locals(dres_t *dres)
{
    return vm_scope_pop(&dres->vm);
}


/********************
 * dres_update_var_stamp
 ********************/
void
dres_update_var_stamp(dres_t *dres, dres_variable_t *var)
{
    if (var->txid != dres->txid) {
        var->txid    = dres->txid;
        var->txstamp = var->stamp;
    }
    var->stamp = dres->stamp;
}


/********************
 * dres_update_target_stamp
 ********************/
void
dres_update_target_stamp(dres_t *dres, dres_target_t *target)
{
    if (target->txid != dres->txid) {
        target->txid    = dres->txid;
        target->txstamp = target->stamp;
    }
    target->stamp = dres->stamp;
}




/*****************************************************************************
 *                       *** misc. dumping/debugging routines                *
 *****************************************************************************/


/********************
 * dres_name
 ********************/
EXPORTED char *
dres_name(dres_t *dres, int id, char *buf, size_t bufsize)
{
    dres_target_t   *target;
    dres_variable_t *variable;

    switch (DRES_ID_TYPE(id)) {
    case DRES_TYPE_TARGET:
        target = dres->targets + DRES_INDEX(id);
        snprintf(buf, bufsize, "%s", target->name);
        break;
    case DRES_TYPE_FACTVAR:
        variable = dres->factvars + DRES_INDEX(id);
        snprintf(buf, bufsize, "$%s", variable->name);
        break;
    case DRES_TYPE_DRESVAR:
        variable = dres->dresvars + DRES_INDEX(id);
        snprintf(buf, bufsize, "&%s", variable->name);
        break;
    default:
        snprintf(buf, bufsize, "<invalid id 0x%x>", id);
    }

    return buf;
}


/********************
 * dres_dump_sort
 ********************/
EXPORTED void
dres_dump_sort(dres_t *dres, int *list)
{
    int  i;
    char buf[32];
   
    for (i = 0; list[i] != DRES_ID_NONE; i++)
        DEBUG(DBG_GRAPH, "  #%03d: 0x%x (%s)\n", i, list[i],
              dres_name(dres, list[i], buf, sizeof(buf)));
}


/********************
 * yyerror
 ********************/
EXPORTED void
yyerror(dres_t *dres, const char *msg)
{
    extern void dres_parse_error(dres_t *, int, const char *, const char *);

    dres_parse_error(dres, lexer_lineno(), msg, yylval.string);
}


#if 0
/********************
 * dres_save
 ********************/
EXPORTED int
dres_save(dres_t *dres, char *path)
{
#define INITIAL_SIZE (64 * 1024)
#define MAX_SIZE     (1024 * 1024)
    
    dres_buf_t *buf, *strbuf;
    dres_t     *copy;
    int         size, status, n;
    unsigned    base;
    FILE       *fp;
    u_int32_t   magic;
    
    size = INITIAL_SIZE;
    buf  = strbuf = NULL;
    fp   = NULL;

 retry:
    if ((buf    = dres_buf_create(size)) == NULL ||
        (strbuf = dres_buf_create(size)) == NULL) {
        status = ENOMEM;
        goto fail;
    }
    
    copy = dres_buf_alloc(buf, sizeof(*copy));

    if ((status = dres_save_targets(dres, copy, buf, strbuf)) != 0)
        goto fail;

    if ((status = dres_save_factvars(dres, copy, buf, strbuf)) != 0)
        goto fail;

    if ((status = dres_save_dresvars(dres, copy, buf, strbuf)) != 0)
        goto fail;

    if ((status = save_initializers(dres, copy, buf, strbuf)) != 0)
        goto fail;

    if ((status = save_methods(dres, copy, buf, strbuf)) != 0)
        goto fail;

    DRES_SET_FLAG(copy, COMPILED);
    copy->flags = htonl(copy->flags);
    
    if ((fp = fopen(path, "w")) == NULL)
        goto fail;
    
    magic = htonl(DRES_MAGIC);
    
    if (fwrite(&magic, sizeof(magic), 1, fp) != 1)
        goto fail;

    n = htonl(buf->used);
    if (fwrite(&n, sizeof(n), 1, fp) != 1)
        goto fail;
    n = htonl(strbuf->used);    
    if (fwrite(&n, sizeof(n), 1, fp) != 1)
        goto fail;
    
    base = htonl((unsigned)buf);
    if (fwrite(&base, sizeof(base), 1, fp) != 1)
        goto fail;

    base = htonl((unsigned)strbuf);
    if (fwrite(&base, sizeof(base), 1, fp) != 1)
        goto fail;

    if (fwrite(buf->data, buf->used, 1, fp) != 1 ||
        fwrite(strbuf->data, strbuf->used, 1, fp) != 1)
        goto fail;

    printf("*** buf size: %d\n", buf->used);
    printf("*** buf base: %x\n", (unsigned)buf);
    printf("*** strbuf size: %d\n", strbuf->used);
    printf("*** strbuf base: %x\n", (unsigned)strbuf);

    fclose(fp);
    return 0;
        
 fail:
    dres_buf_destroy(buf);
    dres_buf_destroy(strbuf);

    if (status == ENOMEM && size < MAX_SIZE) {
        size *= 2;
        goto retry;
    }
    
    if (fp != NULL) {
        fclose(fp);
        unlink(path);
    }
    
    return status;
}


/********************
 * save_initializers
 ********************/
static int
save_initializers(dres_t *dres, dres_t *copy,
                  dres_buf_t *buf, dres_buf_t *strbuf)
{
    dres_initializer_t *src, *dst;
    dres_init_t        *si, *di;
    int                 size, i, j, ninit, nfield;

    if (dres->initializers == NULL)
        return 0;
    
    ninit = 0;
    for (src = dres->initializers; src != NULL; src = src->next)
        ninit++;

    size = ninit * sizeof(*dres->initializers);
    copy->initializers = dres_buf_alloc(buf, size);

    if (copy->initializers == NULL)
        return ENOMEM;

    for (src = dres->initializers, i = 0; src; src = src->next, i++) {
        dst = copy->initializers + i;
        
        dst->variable = htonl(src->variable);
        if (i < ninit - 1)
            dst->next = dst + 1;

        nfield = 0;
        for (si = src->fields; si != NULL; si = si->next)
            nfield++;

        size = nfield * sizeof(*src->fields);
        dst->fields = dres_buf_alloc(buf, size);

        if (dst->fields == NULL)
            return ENOMEM;
        
        for (si = src->fields, j = 0; si != NULL; si = si->next) {
            di = dst->fields + j;
         
            di->field.name = dres_buf_stralloc(strbuf, si->field.name);

            if (di->field.name == NULL)
                return ENOMEM;
   
            di->field.value.type = htonl(si->field.value.type);
            
            switch (si->field.value.type) {
            case DRES_TYPE_INTEGER:
            case DRES_TYPE_DRESVAR:
                di->field.value.v.i = htonl(si->field.value.v.i);
                break;
            case DRES_TYPE_STRING:
                di->field.value.v.s = 
                    dres_buf_stralloc(strbuf, si->field.value.v.s);
                if (di->field.value.v.s == NULL)
                    return ENOMEM;
                break;
            case DRES_TYPE_DOUBLE:
                /*
                 * XXX TODO: How to do this portably ?
                 */
                printf("*** implement doubles, please (%s@%s:%d)... ***\n",
                       __FUNCTION__, __FILE__, __LINE__);
                break;
            }
            
            if (j < nfield - 1)
                di->next = di + 1;
        }
    }

    return 0;
}


/********************
 * save_methods
 ********************/
int
save_methods(dres_t *dres, dres_t *copy, dres_buf_t *buf, dres_buf_t *strbuf)
{
    vm_method_t *sm, *dm;
    int          size, i;

    copy->vm.nmethod = htonl(dres->vm.nmethod);

    if (dres->vm.nmethod <= 0)
        return 0;
    
    size = dres->vm.nmethod * sizeof(*dres->vm.methods);
    copy->vm.methods = dres_buf_alloc(buf, size);

    if (copy->vm.methods == NULL)
        return ENOMEM;
    
    for (i = 0, sm = dres->vm.methods, dm = copy->vm.methods;
         i < dres->vm.nmethod;
         i++, sm++, dm++) {
        dm->id   = htonl(sm->id);
        dm->name = dres_buf_stralloc(strbuf, sm->name);
        
        if (dm->name == NULL)
            return ENOMEM;
    }       

    return 0;
}


/********************
 * dres_load
 ********************/
EXPORTED dres_t *
dres_load(char *path)
{
    dres_t    *dres;
    int        fd;
    u_int32_t  magic;
    int        size, strsize;
    void      *base, *strbase, *buf;
    ptrdiff_t  diff, strdiff;
    
    buf = NULL;
    
    if ((fd = open(path, O_RDONLY)) < 0)
        goto fail;
    
    if (read(fd, &magic, sizeof(magic)) != sizeof(magic))
        goto fail;
    
    if (ntohl(magic) != DRES_MAGIC) {
        errno = EINVAL;
        goto fail;
    }
    
    if (read(fd, &size, sizeof(size)) != sizeof(size))
        goto fail;
    size = ntohl(size);

    if (read(fd, &strsize, sizeof(strsize)) != sizeof(strsize))
        goto fail;
    strsize = ntohl(strsize);

    if ((buf = malloc(size + strsize)) == NULL)
        goto fail;

    if (read(fd, &base, sizeof(unsigned)) != sizeof(unsigned) ||
        read(fd, &strbase, sizeof(unsigned)) != sizeof(unsigned))
        goto fail;
    
    base    = (void *)ntohl((unsigned)base);
    strbase = (void *)ntohl((unsigned)strbase);

    printf("*** buf size: %d\n", size);
    printf("*** buf base: %x\n", (unsigned)base);
    printf("*** strbuf size: %d\n", strsize);
    printf("*** strbuf base: %x\n", (unsigned)strbase);
    
    if (read(fd, buf, size + strsize) != size + strsize)
        goto fail;

    dres = (dres_t *)buf;

    dres->flags = ntohl(dres->flags);
    DRES_SET_FLAG(dres, COMPILED);

    diff    = buf - base;
    strdiff = buf + size - strbase;

    printf("*** diff: %d\n", diff);
    printf("*** strdiff: %d\n", strdiff);
    
    printf("*** sizeof(dres_t *) = %d\n", sizeof(dres));

    printf("*** targets offset: %d\n", (void *)dres->targets - (void *)base);

    if ((errno = dres_load_targets(dres, diff, strdiff)) != 0)
        goto fail;
    
    if ((errno = dres_load_factvars(dres, diff, strdiff)) != 0)
        goto fail;
    
    if ((errno = dres_load_dresvars(dres, diff, strdiff)) != 0)
        goto fail;
    
    if ((errno = load_initializers(dres, diff, strdiff)) != 0)
        goto fail;
    
    if ((errno = load_methods(dres, diff, strdiff)) != 0)
        goto fail;
    
    close(fd);

    if (initialize_variables(dres) != 0 || finalize_variables(dres) != 0) {
        errno = EINVAL;
        goto fail;
    }
    
    return dres;

 fail:
    if (fd >= 0)
        close(fd);

    if (buf != NULL)
        free(buf);

    return NULL;
}


/********************
 * load_initializers
 ********************/
static int
load_initializers(dres_t *dres, ptrdiff_t diff, ptrdiff_t strdiff)
{
    dres_initializer_t *init;
    dres_init_t        *f;

    if (dres->initializers == NULL)
        return 0;

    DRES_RELOCATE(dres->initializers, diff);
    
    for (init = dres->initializers; init; init = init->next) {
        init->variable = ntohl(init->variable);

        if (init->next != NULL)
            DRES_RELOCATE(init->next, diff);
        
        if (init->fields != NULL)
            DRES_RELOCATE(init->fields, diff);
        
        for (f = init->fields; f != NULL; f = f->next) {
            f->field.name       += strdiff;
            f->field.value.type  = ntohl(f->field.value.type);

            switch (f->field.value.type) {
            case DRES_TYPE_INTEGER:
            case DRES_TYPE_DRESVAR:
                f->field.value.v.i = ntohl(f->field.value.v.i);
                break;
            case DRES_TYPE_STRING:
                f->field.value.v.s += strdiff;
                break;
            case DRES_TYPE_DOUBLE:
                /*
                 * XXX TODO: How to do this portably ?
                 */
                printf("*** implement doubles, please (%s@%s:%d)... ***\n",
                       __FUNCTION__, __FILE__, __LINE__);
                break;
            }

            if (f->next != NULL)
                DRES_RELOCATE(f->next, diff);
        }
    }
    
    return 0;
}


/********************
 * load_methods
 ********************/
static int
load_methods(dres_t *dres, ptrdiff_t diff, ptrdiff_t strdiff)
{
    vm_method_t *m;
    int          i;

    dres->vm.nmethod = ntohl(dres->vm.nmethod);

    if (dres->vm.nmethod <= 0)
        return 0;
    
    if (dres->vm.methods == NULL)
        return EINVAL;

    DRES_RELOCATE(dres->vm.methods, diff);

    for (i = 0, m = dres->vm.methods; i < dres->vm.nmethod; i++, m++) {
        m->id = ntohl(m->id);

        DRES_RELOCATE(m->name, strdiff);
    }       

    return 0;
}

#endif


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
