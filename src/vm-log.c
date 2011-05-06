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
#include <string.h>
#include <errno.h>

#include <dres/compiler.h>
#include <dres/vm.h>

static void (*logger)(vm_log_level_t level, const char *format, va_list ap);
static vm_log_level_t log_level = VM_LOG_INFO;

/********************
 * vm_log
 ********************/
void
vm_log(vm_log_level_t level, const char *format, ...)
{
    const char *prefix;
    FILE       *out;
    va_list     ap;

    va_start(ap, format);

    if (logger != NULL)
        logger(level, format, ap);
    else {
        if (level > log_level)
            return;

        switch (level) {
        case VM_LOG_FATAL:   out = stderr; prefix = "C:"; break;
        case VM_LOG_ERROR:   out = stderr; prefix = "E:"; break;
        case VM_LOG_WARNING: out = stderr; prefix = "W:"; break;
        case VM_LOG_NOTICE:  out = stdout; prefix = "N:"; break;
        case VM_LOG_INFO:    out = stdout; prefix = "I:"; break;
        default:                                          return;
        }

        /*
         * Hmm... if we'd care about threaded apps maybe we should prepare the
         * message in a buffer then write(2) it to fileno(out) for atomicity.
         */

        fprintf(out , "%s ", prefix);
        vfprintf(out, format , ap);
        fprintf(out, "\n");
    }
    
    va_end(ap);
}


/********************
 * vm_set_logger
 ********************/
void
vm_set_logger(void (*app_logger)(vm_log_level_t, const char *, va_list))
{
    logger = (void (*)(vm_log_level_t, const char *, va_list))app_logger;
}


/********************
 * vm_set_log_level
 ********************/
vm_log_level_t
vm_set_log_level(vm_log_level_t level)
{
    vm_log_level_t old_level;

    old_level = log_level;
    log_level = level;
    
    return old_level;
}

/* 
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 * vim:set expandtab shiftwidth=4:
 */
