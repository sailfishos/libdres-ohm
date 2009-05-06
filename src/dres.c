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
#include "parser.h"


/* trace flags */
int DBG_GRAPH, DBG_VAR, DBG_RESOLVE, DBG_ACTION, DBG_VM;

TRACE_DECLARE_MODULE(trcdres, "dres",
    TRACE_FLAG("graph"  , "dependency graph"    , &DBG_GRAPH),
    TRACE_FLAG("var"    , "variable handling"   , &DBG_VAR),
    TRACE_FLAG("resolve", "dependency resolving", &DBG_RESOLVE),
    TRACE_FLAG("action" , "action processing"   , &DBG_ACTION),
    TRACE_FLAG("vm"     , "VM execution"        , &DBG_VM));
    

extern int   lexer_open(char *path);
extern int   lexer_lineno(void);
extern int   yyparse(dres_t *dres);

int  initialize_variables(dres_t *dres);
int  finalize_variables  (dres_t *dres);
static void free_initializers   (dres_t *dres);
static int  finalize_actions    (dres_t *dres);
static int  check_undefined     (dres_t *dres);

static int  push_locals(dres_t *dres, char **locals);
static int  pop_locals (dres_t *dres);



/********************
 * dres_set_logger
 ********************/
EXPORTED void
dres_set_logger(void (*logger)(dres_log_level_t, const char *, ...))
{
    vm_set_logger((void (*)(vm_log_level_t, const char *, ...))logger);
}


/********************
 * dres_open
 ********************/
EXPORTED dres_t *
dres_open(char *file)
{
    struct stat st;
    char        path[PATH_MAX], *suffix;
    dres_t     *dres;
    size_t      len;
    int         cid;

    trace_init();
    cid = TRACE_DEFAULT_CONTEXT;
    trace_add_module(cid, &trcdres);

    
    /*
     * try to load the given file if it is found and a regular file
     */

    if (stat(file, &st) == 0 && S_ISREG(st.st_mode)) {
        if ((dres = dres_load(file)) != NULL ||
            (dres = dres_parse_file(file)) != NULL)
            return dres;

        return NULL;
    }


    /*
     * otherwise try to load it with binary and plain suffices
     */

    if ((len = strlen(file)) >= sizeof(path) ||
        len + sizeof(DRES_SUFFIX_BINARY) > sizeof(path) ||
        len + sizeof(DRES_SUFFIX_PLAIN) > sizeof(path)) {
        errno = EOVERFLOW;
        return NULL;
    }
    
    strcpy(path, file);
    suffix = path + len;
    *suffix++ = '.';
    
    strcpy(suffix, DRES_SUFFIX_BINARY);
    if ((dres = dres_load(path)) != NULL)
        return dres;
    
    strcpy(suffix, DRES_SUFFIX_PLAIN);
    return dres_parse_file(path);
}


/********************
 * dres_init
 ********************/
EXPORTED dres_t *
dres_init(char *prefix)
{
    dres_t *dres;
    int     status;
    
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
        DRES_WARNING("ignoring deprecated DRES prefix \"%s\"", prefix);
    
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

    if (DRES_TST_FLAG(dres, COMPILED))
        free(dres);
    else {
        dres_free_targets(dres);
        dres_free_factvars(dres);
        dres_free_dresvars(dres);
        free_initializers(dres);
        vm_exit(&dres->vm);
        FREE(dres);
    }
}


/********************
 * dres_parse_file
 ********************/
EXPORTED dres_t *
dres_parse_file(char *path)
{
#define FAIL(err) do { status = err; goto fail; } while (0)
    dres_t *dres = NULL;
    int     status, i;

    if (path == NULL)
        FAIL(EINVAL);
    
    if ((status = lexer_open(path)) != 0)
        FAIL(status);
    
    if ((dres = dres_init(NULL)) == NULL)
        FAIL(errno);

    if ((status = yyparse(dres)) != 0 ||
        (status = check_undefined(dres)) != 0 ||
        (status = initialize_variables(dres)) != 0 ||
        (status = finalize_variables(dres)) != 0)
        FAIL(status);
    
    dres->vm.nlocal = dres->ndresvar;
    for (i = 0; i < dres->ndresvar; i++)
        vm_set_varname(&dres->vm, i, dres->dresvars[i].name);
    
    return dres;
    
 fail:
    if (dres != NULL)
        dres_exit(dres);
    
    errno = status;
    return NULL;
#undef FAIL
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
 * dres_free_value
 ********************/
void
dres_free_value(dres_value_t *val)
{
    if (val && val->type == DRES_TYPE_STRING)
        FREE(val->v.s);
}


/********************
 * dres_free_field
 ********************/
void
dres_free_field(dres_field_t *f)
{
    FREE(f->name);
    dres_free_value(&f->value);
}


/********************
 * free_inits
 ********************/
static void
free_inits(dres_init_t *init)
{
    dres_init_t *p, *n;

    for (p = init; p != NULL; p = n) {
        n = p->next;
        dres_free_field(&p->field);
        FREE(p);
    }
}


/********************
 * free_initializers
 ********************/
void
free_initializers(dres_t *dres)
{
    dres_initializer_t *p, *n;

    for (p = dres->initializers; p != NULL; p = n) {
        n = p->next;
        free_inits(p->fields);
        FREE(p);
    }
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
 * check_undefined
 ********************/
static int
check_undefined(dres_t *dres)
{
    dres_target_t *target, *prereq;
    int            i, j, id;

    for (i = 0, target = dres->targets; i < dres->ntarget; i++, target++) {
        if (target->prereqs == NULL)
            continue;
        for (j = 0; j < target->prereqs->nid; j++) {
            id = target->prereqs->ids[j];
            if (DRES_ID_TYPE(id) != DRES_TYPE_TARGET)
                continue;
            prereq = dres->targets + DRES_INDEX(id);
            if (!DRES_IS_DEFINED(prereq->id)) {
                DRES_ERROR("Undefined target '%s' referenced from '%s'.",
                           prereq->name, target->name);
                return ENOENT;
            }
        }
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
    dres_target_t *target;
    int            i, status;

    if (DRES_TST_FLAG(dres, ACTIONS_FINALIZED))
        return 0;
    
    status = 0;
    for (i = 0, target = dres->targets; i < dres->ntarget; i++, target++) {
        DRES_INFO("Compiling actions for target %s...", target->name);
        if ((status = dres_compile_target(dres, target)) != 0)
            return status;
    }

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

    if (DRES_TST_FLAG(dres, TARGETS_FINALIZED))
        return 0;

    for (i = 0, target = dres->targets; i < dres->ntarget; i++, target++) {
        DRES_INFO("Compiling dependency graph for target %s...", target->name);

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
                DRES_ACTION_ERROR(status);
    
    if (!DRES_TST_FLAG(dres, TARGETS_FINALIZED))
        if ((status = finalize_targets(dres)) != 0)
            DRES_ACTION_ERROR(status);
    
    if (goal != NULL) {
        if ((target = dres_lookup_target(dres, goal)) == NULL)
            DRES_ACTION_ERROR(EINVAL);
    }
    else {
        target = dres->targets;
        goal   = target->name;
    }
    
    if (!DRES_IS_DEFINED(target->id))
        DRES_ACTION_ERROR(EINVAL);

    if (!DRES_TST_FLAG(dres, TRANSACTION_ACTIVE)) {
        if (!dres_store_tx_new(dres))
            DRES_ACTION_ERROR(EINVAL);

        dres->txid++;
        own_tx = 1;
    }
    else
        own_tx = 0;

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
        
            if ((status = dres_check_target(dres, id)) <= 0)
                break;
        }
    }
    
    if (locals != NULL)
        pop_locals(dres);
    
    if (status > 0) {
        dres_update_target_stamp(dres, target);
        if (own_tx)
            dres_store_tx_commit(dres);
    }
    else {
    rollback:
        if (own_tx)
            dres_store_tx_rollback(dres);
    }
    
    DEBUG(DBG_RESOLVE, "updated of goal %s done with status %d (%s)",
          goal, status, status < 0 ? "error" : (status ? "success" : "failed"));

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
    vm_value_t   v;
    char        *name, *value;
    int          err, id, i;
    unsigned int type;
    
    if (locals == NULL)
        return 0;
    
    if ((err = vm_scope_push(&dres->vm)) != 0)
        return err;
    
    i = 0;
    while (locals[i] != NULL) {
        name = locals[i++];
        type = (int)locals[i++];
        
        if (type < 0xff) {
            value = locals[i++];
            switch (type) {
            case 's': type = DRES_TYPE_STRING;  v.s = value;            break;
            case 'i': type = DRES_TYPE_INTEGER; v.i = (int)value;       break;
            case 'd': type = DRES_TYPE_DOUBLE;  v.d = *(double *)value; break;
            default:
                DRES_ERROR("local value of invalid type 0x%x", type);
                FAIL(EINVAL);
            }
        }
        else {
            v.s  = (char *)type;
            type = DRES_TYPE_STRING;
        }
        
        if ((id = dres_dresvar_id(dres, name)) == DRES_ID_NONE) {
            DRES_ERROR("cannot set undeclared variable &%s", name);
            FAIL(ENOENT);
        }
            
        if ((err = vm_scope_set(dres->vm.scope, id, type, v)) != 0)
            FAIL(err);
    }
    
    return 0;

 fail:
    vm_scope_pop(&dres->vm);
    return err;
#undef FAIL
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


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
