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


#ifndef __DRES_DEBUG_H__
#define __DRES_DEBUG_H__

#include <simple-trace/simple-trace.h>

#define DRES_DEBUG(flag, format, args...) do {			   \
    trace_printf((flag), format, ## args);			   \
  } while (0)
#define DEBUG DRES_DEBUG

#define DRES_DEBUG_ON(flag) trace_flag_tst(flag)
#define DEBUG_ON DRES_DEBUG_ON

extern int DBG_GRAPH, DBG_VAR, DBG_RESOLVE, DBG_ACTION, DBG_VM;



#endif /* __DRES_DEBUG_H__ */
