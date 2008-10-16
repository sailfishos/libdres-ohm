#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <math.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <dres/dres.h>
#include <dres/compiler.h>
#include <dres/vm.h>

#if (!defined(_XOPEN_SOURCE) && !defined(_ISOC99_SOURCE)) || _XOPEN_SOURCE < 600
double trunc(double);
#endif

static int compile_value  (dres_t *dres, dres_value_t *value, vm_chunk_t *code);
static int compile_varref (dres_t *dres, dres_varref_t *vr, vm_chunk_t *code);
static int compile_call   (dres_t *dres, dres_call_t *call, vm_chunk_t *code);
static int compile_assign (dres_t *dres, dres_varref_t *lval, vm_chunk_t *code);
static int compile_discard(dres_t *dres, vm_chunk_t *code);
static int compile_debug  (const char *info, vm_chunk_t *code);


static int save_initializers(dres_t *dres, dres_buf_t *buf);
static int load_initializers(dres_t *dres, dres_buf_t *buf);
static int save_methods     (dres_t *dres, dres_buf_t *buf);
static int load_methods     (dres_t *dres, dres_buf_t *buf);

extern int initialize_variables(dres_t *dres); /* XXX TODO: kludge */
extern int finalize_variables  (dres_t *dres); /* XXX TODO: kludge */



/********************
 * dres_compile_target
 ********************/
int
dres_compile_target(dres_t *dres, dres_target_t *target)
{
    dres_action_t *a;
    int            status;

    if (target->actions == NULL)
        return 0;

    if (target->code == NULL)
        if ((target->code = vm_chunk_new(16)) == NULL)
            return ENOMEM;
    
    for (a = target->actions; a != NULL; a = a->next)
        if ((status = dres_compile_action(dres, a, target->code)) != 0) {
            printf("failed to compile the following action of target %s\n",
                   target->name);
            dres_dump_action(dres, a);
            return status;
        }

    return 0;
}


/********************
 * dres_compile_action
 ********************/
int
dres_compile_action(dres_t *dres, dres_action_t *action, vm_chunk_t *code)
{
    int  status;
    char dbg[256];

    if (dres_print_action(dres, action, dbg, sizeof(dbg)) > 0)
        compile_debug(dbg, code);
    
    switch (action->type) {
    case DRES_ACTION_VALUE:
        if (action->lvalue.variable == DRES_ID_NONE)
            return EINVAL;
        if ((status = compile_value(dres, &action->value, code)) != 0)
            return status;
        break;
    case DRES_ACTION_VARREF:
        if (action->lvalue.variable == DRES_ID_NONE)
            return EINVAL;
        if ((status = compile_varref(dres, &action->rvalue, code)) != 0)
            return status;
        break;
    case DRES_ACTION_CALL:
        if ((status = compile_call(dres, action->call, code)) != 0)
            return status;
        break;
    default:
        return EINVAL;
    }

    if (action->lvalue.variable != DRES_ID_NONE)
        status = compile_assign(dres, &action->lvalue, code);
    else
        status = compile_discard(dres, code);

    return status;
}


#define PUSH_VALUE(code, fail, err, value) do {                         \
        switch ((value)->type) {                                        \
        case DRES_TYPE_INTEGER:                                         \
            VM_INSTR_PUSH_INT((code), fail, err, (value)->v.i);         \
            break;                                                      \
        case DRES_TYPE_DOUBLE:                                          \
            VM_INSTR_PUSH_DOUBLE((code), fail, err, (value)->v.d);      \
            break;                                                      \
        case DRES_TYPE_STRING:                                          \
            VM_INSTR_PUSH_STRING((code), fail, err, (value)->v.s);      \
            break;                                                      \
        case DRES_TYPE_FACTVAR: { /* XXX TODO $foo[...] ??? */          \
            const char *__f = dres_factvar_name(dres, (value)->v.id);   \
            if (__f == NULL) {                                          \
                err = EINVAL;                                           \
                goto fail;                                              \
            }                                                           \
            VM_INSTR_PUSH_GLOBAL((code), fail, err, __f);               \
        }                                                               \
            break;                                                      \
        case DRES_TYPE_DRESVAR:                                         \
            VM_INSTR_GET_LOCAL((code), fail, err, (value)->v.id);       \
            break;                                                      \
        default:                                                        \
            err = EINVAL;                                               \
            goto fail;                                                  \
        }                                                               \
    } while (0)


/********************
 * compile_value
 ********************/
static int
compile_value(dres_t *dres, dres_value_t *value, vm_chunk_t *code)
{
    int err;

    PUSH_VALUE(code, fail, err, value);
    return 0;

 fail:
    printf("*** %s: code generation failed (%d: %s)\n", __FUNCTION__,
           err, strerror(err));
    return err;
}


/********************
 * compile_varref
 ********************/
static int
compile_varref(dres_t *dres, dres_varref_t *varref, vm_chunk_t *code)
{
#define FAIL(ec) do { err = (ec); goto fail; } while (0)
    dres_select_t *s;
    dres_value_t  *value;
    const char    *name;
    int            n, err;
    
    if (varref->variable == DRES_ID_NONE)
        return EINVAL;

    switch (DRES_ID_TYPE(varref->variable)) {
    case DRES_TYPE_FACTVAR:
        if ((name = dres_factvar_name(dres, varref->variable)) == NULL)
            FAIL(ENOENT);
        VM_INSTR_PUSH_GLOBAL(code, fail, err, name);

        if (varref->selector != NULL) {
            for (n = 0, s = varref->selector; s != NULL; n++, s = s->next) {
                value = &s->field.value;
                PUSH_VALUE(code, fail, err, value);            
                VM_INSTR_PUSH_STRING(code, fail, err, s->field.name);
            }
            VM_INSTR_FILTER(code, fail, err, n);
        }

        if (varref->field != NULL) {
            printf("*** %s: implement VM GET FIELD...", __FUNCTION__);
            FAIL(EOPNOTSUPP);
        }
        break;
        
    case DRES_TYPE_DRESVAR:
        /* XXX TODO: implement me */
        FAIL(EOPNOTSUPP);
        
    default:
        FAIL(EINVAL);
    }

    return 0;

 fail:
    printf("*** %s: code generation failed (%d: %s)\n", __FUNCTION__,
           err, strerror(err));
    return err;
#undef FAIL
}


/********************
 * compile_call
 ********************/
static int
compile_call(dres_t *dres, dres_call_t *call, vm_chunk_t *code)
{
#define FAIL(ec) do { err = (ec); goto fail; } while (0)
    dres_arg_t   *arg;
    dres_local_t *var;
    int           narg, nvar, err, id;
    
    if ((id = vm_method_id(&dres->vm, call->name)) < 0)
        FAIL(ENOENT);
    
    for (arg = call->args, narg = 0; arg != NULL; arg = arg->next, narg++) {
        PUSH_VALUE(code, fail, err, &arg->value);
    }
    
    for (var = call->locals, nvar = 0; var != NULL; var = var->next, nvar++) {
        PUSH_VALUE(code, fail, err, &var->value);
        VM_INSTR_PUSH_INT(code, fail, err, DRES_INDEX(var->id));
    }
    if (nvar > 0)
        VM_INSTR_PUSH_LOCALS(code, fail, err, nvar);
    
    VM_INSTR_PUSH_INT(code, fail, err, id);
    VM_INSTR_CALL(code, fail, err, narg);
    
    if (nvar > 0)
        VM_INSTR_POP_LOCALS(code, fail, err);
    
    return 0;

 fail:
    printf("*** %s: code generation failed (%d: %s)\n", __FUNCTION__,
           err, strerror(err));
    return err;
}


/********************
 * compile_assign
 ********************/
int
compile_assign(dres_t *dres, dres_varref_t *lvalue, vm_chunk_t *code)
{
#define FAIL(ec) do { err = (ec); goto fail; } while (0)
    dres_select_t *s;
    dres_value_t  *value;
    const char    *name;
    int            n, err;
    int            update, filter;
    
    if (lvalue->variable == DRES_ID_NONE)
        FAIL(EINVAL);

    switch (DRES_ID_TYPE(lvalue->variable)) {
    case DRES_TYPE_FACTVAR:
        if ((name = dres_factvar_name(dres, lvalue->variable)) == NULL)
            FAIL(ENOENT);
        VM_INSTR_PUSH_GLOBAL(code, fail, err, name);
        
        update = filter = FALSE;
        if (lvalue->selector != NULL) {
            for (n = 0, s = lvalue->selector; s != NULL; s = s->next) {
                if (s->field.value.type == DRES_TYPE_UNKNOWN) {
                    update = TRUE;
                    continue;
                }
                else {
                    filter = TRUE;
                    value = &s->field.value;
                    PUSH_VALUE(code, fail, err, value);            
                    VM_INSTR_PUSH_STRING(code, fail, err, s->field.name);
                    n++;
                }
            }
            if (filter)
                VM_INSTR_FILTER(code, fail, err, n);
        }

        if (lvalue->field != NULL) {
            if (update)
                FAIL(EINVAL);
            
            VM_INSTR_PUSH_STRING(code, fail, err, lvalue->field);
            VM_INSTR_SET_FIELD(code, fail, err);
        }
        else {
            if (update) {
                for (n = 0, s = lvalue->selector; s != NULL; s = s->next) {
                    if (s->field.value.type != DRES_TYPE_UNKNOWN)
                        continue;
                    VM_INSTR_PUSH_STRING(code, fail, err, s->field.name);
                    n++;
                }
                VM_INSTR_UPDATE(code, fail, err, n);
            }
            else
                VM_INSTR_SET(code, fail, err);
        }
        break;
        
    case DRES_TYPE_DRESVAR:
        /* XXX TODO: implement me */
        FAIL(EOPNOTSUPP);
        
    default:
        FAIL(EINVAL);
    }

    return 0;

 fail:
    printf("*** %s: code generation failed (%d: %s)\n", __FUNCTION__,
           err, strerror(err));
    return err;
#undef FAIL
}


/********************
 * compile_discard
 ********************/
int
compile_discard(dres_t *dres, vm_chunk_t *code)
{
    int err;
    
    VM_INSTR_POP_DISCARD(code, fail, err);
    
    return 0;

 fail:
    printf("*** %s: code generation failed (%d: %s)\n", __FUNCTION__,
           err, strerror(err));
    return err;

    (void)dres;
}


/********************
 * compile_debug
 ********************/
static int
compile_debug(const char *info, vm_chunk_t *code)
{
    int err;
    
    VM_INSTR_DEBUG(code, fail, err, info);
    
    return 0;
    
 fail:
    printf("*** %s: code generation failed for debug info \"%s\" (%d: %s)\n",
           __FUNCTION__, info, err, strerror(err));
    return err;
}

/*****************************************************************************
 *                   *** precompiled/binary rule support ***                 *
 *****************************************************************************/


/********************
 * dres_save
 ********************/
EXPORTED int
dres_save(dres_t *dres, char *path)
{
#define INITIAL_SIZE (64 * 1024)
#define MAX_SIZE     (1024 * 1024)
    
    dres_buf_t *buf;
    int         size, status;
    FILE       *fp;
    
    size = INITIAL_SIZE;
    buf  = NULL;
    fp   = NULL;

 retry:
    if ((buf = dres_buf_create(size, size)) == NULL) {
        status = ENOMEM;
        goto fail;
    }
    
    if ((status = dres_save_targets(dres, buf)) != 0)
        goto fail;

    if ((status = dres_save_factvars(dres, buf)) != 0)
        goto fail;

    if ((status = dres_save_dresvars(dres, buf)) != 0)
        goto fail;

    if ((status = save_initializers(dres, buf)) != 0)
        goto fail;

    if ((status = save_methods(dres, buf)) != 0)
        goto fail;

    if ((fp = fopen(path, "w")) == NULL)
        goto fail;

#define HTONL(_f) buf->header._f = htonl(buf->header._f)
    buf->header.magic   = htonl(DRES_MAGIC);
    buf->header.ssize   = htonl(buf->sused);
    HTONL(ntarget);
    HTONL(nprereq);
    HTONL(ncode);
    HTONL(sinstr);
    HTONL(ndependency);
    HTONL(nvariable);
    HTONL(ninit);
    HTONL(nfield);
    HTONL(nmethod);
    
    if (fwrite(&buf->header, sizeof(buf->header), 1, fp) != 1)
        goto fail;
    
    if (fwrite(buf->strings, buf->sused, 1, fp) != 1 ||
        fwrite(buf->data   , buf->dused, 1, fp) != 1)
        goto fail;

    fclose(fp);
    
    return 0;
        
 fail:
    dres_buf_destroy(buf);

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
save_initializers(dres_t *dres, dres_buf_t *buf)
{
    dres_initializer_t *init;
    dres_init_t        *f;
    int                 ninit, nfield;

    ninit = 0;
    for (init = dres->initializers; init != NULL; init = init->next)
        ninit++;

    dres_buf_ws32(buf, ninit);
    buf->header.ninit = ninit;

    for (init = dres->initializers; init != NULL; init = init->next) {
        dres_buf_ws32(buf, init->variable);
        
        nfield = 0;
        for (f = init->fields; f != NULL; f = f->next)
            nfield++;

        dres_buf_ws32(buf, nfield);
        for (f = init->fields; f != NULL; f = f->next) {
            dres_buf_wstr(buf, f->field.name);
            dres_buf_ws32(buf, f->field.value.type);
            
            switch (f->field.value.type) {
            case DRES_TYPE_INTEGER:
            case DRES_TYPE_DRESVAR:
                dres_buf_ws32(buf, f->field.value.v.i);
                break;
            case DRES_TYPE_STRING:
                dres_buf_wstr(buf, f->field.value.v.s);
                break;
            case DRES_TYPE_DOUBLE:
                dres_buf_wdbl(buf, f->field.value.v.d);
                break;
            }
        }

        buf->header.nfield += nfield;
    }

    return 0;
}


/********************
 * save_methods
 ********************/
static int
save_methods(dres_t *dres, dres_buf_t *buf)
{
    vm_method_t *m;
    int          i;

    dres_buf_ws32(buf, dres->vm.nmethod);
    buf->header.nmethod = dres->vm.nmethod;

    for (i = 0, m = dres->vm.methods; i < dres->vm.nmethod; i++, m++) {
        dres_buf_ws32(buf, m->id);
        dres_buf_wstr(buf, m->name);
    }       

    return 0;
}


/********************
 * dres_load
 ********************/
EXPORTED dres_t *
dres_load(char *path)
{
    dres_buf_t     buf;
    dres_header_t *hdr = &buf.header;
    dres_t        *dres;
    int            size, status;
    

    dres = NULL;
    memset(&buf, 0, sizeof(buf));
    
    if ((buf.fd = open(path, O_RDONLY)) < 0)
        goto fail;

    if (read(buf.fd, hdr, sizeof(*hdr)) != sizeof(*hdr))
        goto fail;
    
#define NTOHL(_f) hdr->_f = ntohl(hdr->_f)
    NTOHL(magic);
    NTOHL(ssize);
    NTOHL(ntarget);
    NTOHL(nprereq);
    NTOHL(ncode);
    NTOHL(sinstr);
    NTOHL(ndependency);
    NTOHL(nvariable);
    NTOHL(ninit);
    NTOHL(nfield);
    NTOHL(nmethod);

    if (hdr->magic != DRES_MAGIC) {
        errno = EINVAL;
        goto fail;
    }

#define SIZE(type, _f) (sizeof(type) * hdr->_f)
    
    size  = SIZE(dres_target_t     , ntarget);
    size += SIZE(dres_prereq_t     , nprereq);
    size += SIZE(vm_chunk_t        , ncode);
    size += SIZE(char              , sinstr);
    size += SIZE(int               , ndependency);
    size += SIZE(dres_variable_t   , nvariable);
    size += SIZE(dres_initializer_t, ninit);
    size += SIZE(dres_init_t       , nfield);
    size += SIZE(vm_method_t       , nmethod);

    buf.dsize = size;
    buf.dused = 0;
    buf.ssize = buf.sused = hdr->ssize;
    
    size += sizeof(*dres) + hdr->ssize;

    if ((dres = (dres_t *)ALLOC_ARR(char, size)) == NULL)
        goto fail;

    buf.strings = ((char *)dres) + sizeof(*dres);
    buf.data    = buf.strings + hdr->ssize;
     
    if (read(buf.fd, buf.strings, hdr->ssize) != (ssize_t)hdr->ssize)
        goto fail;
    
    if ((status = dres_load_targets(dres, &buf)) != 0 ||
        (status = dres_load_factvars(dres, &buf)) != 0 ||
        (status = dres_load_dresvars(dres, &buf)) != 0 ||
        (status = load_initializers(dres, &buf)) != 0 ||
        (status = load_methods(dres, &buf)) != 0) {
        errno = status;
        goto fail;
    }
    
    close(buf.fd);

    if (dres_store_init(dres))
        goto fail;
    if ((status = dres_register_builtins(dres)) != 0) {
        errno = status;
        goto fail;
    }
    

#if 0
    dres_dump_targets(dres);
#endif

    DRES_SET_FLAG(dres, COMPILED);
    DRES_SET_FLAG(dres, ACTIONS_FINALIZED);
    DRES_SET_FLAG(dres, TARGETS_FINALIZED);

    VM_SET_FLAG(&dres->vm, COMPILED);
    
    if (initialize_variables(dres) != 0 || finalize_variables(dres) != 0) {
        errno = EINVAL;
        goto fail;
    }

    return dres;


 fail:
    if (buf.fd >= 0)
        close(buf.fd);
    if (dres)
        FREE(dres);
    
    return NULL;
}


/********************
 * load_initializers
 ********************/
static int
load_initializers(dres_t *dres, dres_buf_t *buf)
{
    dres_initializer_t *init, *previ;
    dres_init_t        *f, *prevf;
    int                 ninit, nfield, i, j;

    ninit = dres_buf_rs32(buf);
    
    for (i = 0, previ = NULL; i < ninit; i++, previ = init) {
        if ((init = dres_buf_alloc(buf, sizeof(*init))) == NULL)
            return ENOMEM;
    
        if (previ == NULL)
            dres->initializers = init;
        else
            previ->next = init;
        
        init->variable = dres_buf_rs32(buf);
        
        nfield = dres_buf_rs32(buf);
        
        for (j = 0, prevf = NULL; j < nfield; j++, prevf = f) {
            if ((f = dres_buf_alloc(buf, sizeof(*f))) == NULL)
                return ENOMEM;

            if (prevf == NULL)
                init->fields = f;
            else
                prevf->next = f;

            f->field.name       = dres_buf_rstr(buf);
            f->field.value.type = dres_buf_rs32(buf);
            
            switch (f->field.value.type) {
            case DRES_TYPE_INTEGER:
            case DRES_TYPE_DRESVAR:
                f->field.value.v.i = dres_buf_rs32(buf);
                break;
            case DRES_TYPE_STRING:
                f->field.value.v.s = dres_buf_rstr(buf);
                break;
            case DRES_TYPE_DOUBLE:
                f->field.value.v.d = dres_buf_rdbl(buf);
                break;
            }
        }
    }

    return 0;
}



/********************
 * load_methods
 ********************/
static int
load_methods(dres_t *dres, dres_buf_t *buf)
{
    vm_method_t *m;
    int          i;

    dres->vm.nmethod = dres_buf_rs32(buf);
    dres->vm.methods =
        dres_buf_alloc(buf, dres->vm.nmethod * sizeof(*dres->vm.methods));
    
    if (dres->vm.methods == NULL)
        return ENOMEM;

    for (i = 0, m = dres->vm.methods; i < dres->vm.nmethod; i++, m++) {
        m->id   = dres_buf_rs32(buf);
        m->name = dres_buf_rstr(buf);
    }       

    return 0;
}




/********************
 * dres_buf_create
 ********************/
dres_buf_t *
dres_buf_create(int dsize, int ssize)
{
    dres_buf_t *buf;

    if (ALLOC_OBJ(buf) == NULL)
        goto fail;

    if ((buf->data    = ALLOC_ARR(char, dsize)) == NULL ||
        (buf->strings = ALLOC_ARR(char, ssize)) == NULL)
        goto fail;
        
    buf->dsize = dsize;
    buf->ssize = ssize;

    buf->strings[0] = '\0';
    buf->strings[1] = '\0';
    buf->sused      = 2;

    return buf;

 fail:
    if (buf) {
        FREE(buf->data);
        FREE(buf->strings);
        FREE(buf);
    }

    return NULL;
}


/********************
 * dres_buf_destroy
 ********************/
void
dres_buf_destroy(dres_buf_t *buf)
{
    if (buf) {
        FREE(buf->data);
        FREE(buf->strings);
        FREE(buf);
    }
}


/********************
 * dres_buf_alloc
 ********************/
void *
dres_buf_alloc(dres_buf_t *buf, size_t size)
{
    char *ptr;
    
    if (buf->error) {
        errno = buf->error;
        return NULL;
    }

    if (buf->dsize - buf->dused < size) {
        ptr = NULL;
        buf->error = errno = ENOMEM;
    }
    else {
        ptr = (void *)buf->data + buf->dused;
        buf->dused += size;
    }

    return ptr;
}


/********************
 * dres_buf_stralloc
 ********************/
char *
dres_buf_stralloc(dres_buf_t *buf, char *str)
{
    char *ptr;
    int   size;
    
    if (buf->error) {
        errno = buf->error;
        return NULL;
    }

    if (str == NULL)
        return buf->strings;

    if (str[0] == '\0')
        return buf->strings + 1;
        
    /* XXX TODO: fold non-empty identical strings as well */
    size = strlen(str) + 1;
    if ((size_t)(buf->ssize - buf->sused) < strlen(str) + 1) {
        buf->error = errno = ENOMEM;
        return NULL;
    }

    ptr         = buf->strings + buf->sused;
    buf->sused += size;

    strcpy(ptr, str);

    return ptr;
}


/********************
 * dres_buf_ws16
 ********************/
int
dres_buf_ws16(dres_buf_t *buf, int16_t i)
{
    int16_t *ptr;

    if ((ptr = dres_buf_alloc(buf, sizeof(int16_t))) == NULL)
        return ENOMEM;

    *ptr = htons(i);
    return 0;
}


/********************
 * dres_buf_wu16
 ********************/
int
dres_buf_wu16(dres_buf_t *buf, u_int16_t i)
{
    u_int16_t *ptr;

    if ((ptr = dres_buf_alloc(buf, sizeof(u_int16_t))) == NULL)
        return ENOMEM;

    *ptr = htons(i);
    return 0;
}


/********************
 * dres_buf_ws32
 ********************/
int
dres_buf_ws32(dres_buf_t *buf, int32_t i)
{
    int32_t *ptr;

    if ((ptr = dres_buf_alloc(buf, sizeof(int32_t))) == NULL)
        return ENOMEM;

    *ptr = htonl(i);
    return 0;
}


/********************
 * dres_buf_wu32
 ********************/
int
dres_buf_wu32(dres_buf_t *buf, u_int32_t i)
{
    u_int32_t *ptr;

    if ((ptr = dres_buf_alloc(buf, sizeof(u_int32_t))) == NULL)
        return ENOMEM;

    *ptr = htonl(i);
    return 0;
}


/********************
 * dres_buf_wstr
 ********************/
int
dres_buf_wstr(dres_buf_t *buf, char *str)
{
    u_int32_t  offs;
    char      *ptr;

    if ((ptr = dres_buf_stralloc(buf, str)) == NULL)
        return ENOMEM;
    
    offs = ptr - buf->strings;
    return dres_buf_wu32(buf, offs);
}


/********************
 * dres_buf_wbuf
 ********************/
int
dres_buf_wbuf(dres_buf_t *buf, char *data, int size)
{
    char *ptr;

    if ((ptr = dres_buf_alloc(buf, size)) == NULL)
        return ENOMEM;

    memcpy(ptr, data, size);
    return 0;
}


/********************
 * dres_buf_wdbl
 ********************/
int
dres_buf_wdbl(dres_buf_t *buf, double d)
{
    int32_t *integer = dres_buf_alloc(buf, sizeof(*integer));
    int32_t *decimal = dres_buf_alloc(buf, sizeof(*decimal));

    /* XXX TODO fixme, this is _not_ the way to do it. */
    printf("%s@%s:%d: FIXME, please...\n", __FUNCTION__, __FILE__, __LINE__);

    if (integer == NULL || decimal == NULL)
        return ENOMEM;

    *integer = trunc(d);
    if (d < 0.0)
      d = -d;
    d = d - *integer;
    *decimal = (int)(1000 * d);

    *integer = htonl(*integer);
    *decimal = htonl(*decimal);
    
    return 0;
}




/********************
 * dres_buf_rs32
 ********************/
int32_t
dres_buf_rs32(dres_buf_t *buf)
{
    int32_t i;

    if (read(buf->fd, &i, sizeof(i)) != sizeof(i)) {
        i          = 0;
        buf->error = errno;
    }
    
    return ntohl(i);
}


/********************
 * dres_buf_ru32
 ********************/
u_int32_t
dres_buf_ru32(dres_buf_t *buf)
{
    u_int32_t i;

    if (read(buf->fd, &i, sizeof(i)) != sizeof(i)) {
        i          = 0;
        buf->error = errno;
    }
    
    return ntohl(i);
}


/********************
 * dres_buf_rstr
 ********************/
char *
dres_buf_rstr(dres_buf_t *buf)
{
    u_int32_t offs;
    
    if (read(buf->fd, &offs, sizeof(offs)) != sizeof(offs)) {
        buf->error = errno;
        return NULL;
    }

    if ((offs = ntohl(offs)) > buf->ssize) {
        buf->error = EOVERFLOW;
        return NULL;
    }
     
    return buf->strings + offs;
}


/********************
 * dres_buf_rbuf
 ********************/
char *
dres_buf_rbuf(dres_buf_t *buf, int size)
{
    char *ptr;

    if ((ptr = dres_buf_alloc(buf, size)) == NULL) {
        buf->error = ENOMEM;
        return NULL;
    }
    
    if (read(buf->fd, ptr, size) != size) {
        buf->error = errno;
        return NULL;
    }

    return ptr;
}


/********************
 * dres_buf_rdbl
 ********************/
double
dres_buf_rdbl(dres_buf_t *buf)
{
    int32_t integer;
    int32_t decimal;
    double  d;

    /* XXX TODO: fixme, this is _not_ the way to do it */
    if (read(buf->fd, &integer, sizeof(integer)) != sizeof(integer) ||
        read(buf->fd, &decimal, sizeof(decimal)) != sizeof(decimal)) {
        buf->error = errno;
        return 0.0;
    }

    integer = ntohl(integer);
    decimal = ntohl(decimal);
    
    d = 1.0 * integer;
    if (integer < 0)
        d = d - decimal / 1000.0;
    else
        d = d + decimal / 1000.0;
    
    return d;
}



/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
