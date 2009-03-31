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
