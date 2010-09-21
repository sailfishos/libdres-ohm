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
