#ifndef __DRES_DEBUG_H__
#define __DRES_DEBUG_H__

#include <simple-trace/simple-trace.h>

#define DEBUG(flag, format, args...) do {                          \
    trace_printf((flag), format, ## args);			   \
  } while (0)


extern int DBG_GRAPH, DBG_VAR, DBG_RESOLVE, DBG_ACTION, DBG_VM;



#endif /* __DRES_DEBUG_H__ */
