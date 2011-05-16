#ifndef __DRES_PARSER_TYPES__
#define __DRES_PARSER_TYPES__


#define COMMON_TOKEN_FIELDS \
    const char *token;      \
    int         lineno

typedef struct {
    COMMON_TOKEN_FIELDS;
} token_any_t;

typedef struct {
    COMMON_TOKEN_FIELDS;
    char *value;
} token_string_t;

typedef struct {
    COMMON_TOKEN_FIELDS;
    int value;
} token_integer_t;

typedef struct {
    COMMON_TOKEN_FIELDS;
    double value;
} token_double_t;

#endif


/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */


