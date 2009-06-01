#ifndef __OHM_RESOLVER_CONSOLE_H__
#define __OHM_RESOLVER_CONSOLE_H__

#define CONSOLE_PROMPT "ohm-dres> "


typedef struct {
    char  *name;                      /* command name */
    char  *args;                      /* description of arguments */
    char  *description;               /* description of command */
    void (*handler)(int, char *);     /* command handler */
} command_t;


static int  console_init(char *address);
static void console_exit(void);


/* commmonly used console interface */
OHM_IMPORTABLE(int, console_printf, (int id, char *fmt, ...));


#endif /* __OHM_RESOLVER_CONSOLE_H__ */




/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
