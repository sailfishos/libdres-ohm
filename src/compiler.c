#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <dres/dres.h>
#include <dres/vm.h>


static int compile_value (dres_t *dres, dres_value_t *value, vm_chunk_t *code);
static int compile_varref(dres_t *dres, dres_varref_t *vr, vm_chunk_t *code);
static int compile_call  (dres_t *dres, dres_call_t *call, vm_chunk_t *code);
static int compile_assign(dres_t *dres, dres_varref_t *lval, vm_chunk_t *code);



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
        if ((status = dres_compile_action(dres, a, target->code)) != 0)
            return status;

    return 0;
}


/********************
 * dres_compile_action
 ********************/
int
dres_compile_action(dres_t *dres, dres_action_t *action, vm_chunk_t *code)
{
    int status;
       
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

    return status;
}


#define PUSH_VALUE(code, fail, err, value) do {                    \
        switch ((value)->type) {                                   \
        case DRES_TYPE_INTEGER:                                    \
            VM_INSTR_PUSH_INT((code), fail, err, (value)->v.i);    \
            break;                                                 \
        case DRES_TYPE_DOUBLE:                                     \
            VM_INSTR_PUSH_DOUBLE((code), fail, err, (value)->v.d); \
            break;                                                 \
        case DRES_TYPE_STRING:                                     \
            VM_INSTR_PUSH_STRING((code), fail, err, (value)->v.s); \
            break;                                                 \
        default:                                                   \
            err = EINVAL;                                          \
            goto fail;                                             \
        }                                                          \
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
    dres_select_t *s;
    dres_value_t  *value;
    char           name[128];
    int            n, err;
    
    if (varref->variable == DRES_ID_NONE)
        return EINVAL;

    switch (DRES_ID_TYPE(varref->variable)) {
    case DRES_TYPE_FACTVAR:
        dres_name(dres, varref->variable, name, sizeof(name));
        VM_INSTR_PUSH_GLOBAL(code, fail, err, name + 1);

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
            err = EOPNOTSUPP;
            goto fail;
        }
        break;
        
    case DRES_TYPE_DRESVAR:
        /* XXX TODO: implement me */
        err = EOPNOTSUPP;
        goto fail;
        
    default:
        err = EINVAL;
        goto fail;
   }

    return 0;

 fail:
    printf("*** %s: code generation failed (%d: %s)\n", __FUNCTION__,
           err, strerror(err));
    return err;
}


/********************
 * compile_call
 ********************/
static int
compile_call(dres_t *dres, dres_call_t *call, vm_chunk_t *code)
{
    dres_arg_t *arg;
    int         n, err;
    
    for (arg = call->args, n = 0; arg != NULL; arg = arg->next, n++) {
        PUSH_VALUE(code, fail, err, &arg->value);
    }
    
    VM_INSTR_PUSH_STRING(code, fail, err, call->name);
    VM_INSTR_CALL(code, fail, err, n);
   
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
    dres_select_t *s;
    dres_value_t  *value;
    char           name[128];
    int            n, err;
    
    if (lvalue->variable == DRES_ID_NONE)
        return EINVAL;

    switch (DRES_ID_TYPE(lvalue->variable)) {
    case DRES_TYPE_FACTVAR:
        dres_name(dres, lvalue->variable, name, sizeof(name));
        VM_INSTR_PUSH_GLOBAL(code, fail, err, name + 1);

        if (lvalue->selector != NULL) {
            for (n = 0, s = lvalue->selector; s != NULL; n++, s = s->next) {
                value = &s->field.value;
                PUSH_VALUE(code, fail, err, value);            
                VM_INSTR_PUSH_STRING(code, fail, err, s->field.name);
            }
            VM_INSTR_FILTER(code, fail, err, n);
        }

        if (lvalue->field != NULL) {
            VM_INSTR_PUSH_STRING(code, fail, err, lvalue->field);
            VM_INSTR_SET_FIELD(code, fail, err);
        }
        else
            VM_INSTR_SET(code, fail, err);
        break;
        
    case DRES_TYPE_DRESVAR:
        /* XXX TODO: implement me */
        err = EOPNOTSUPP;
        goto fail;
        
    default:
        err = EINVAL;
        goto fail;
   }

    return 0;

 fail:
    printf("*** %s: code generation failed (%d: %s)\n", __FUNCTION__,
           err, strerror(err));
    return err;
}


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
