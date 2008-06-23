#ifndef __DRES_DEBUG_H__
#define __DRES_DEBUG_H__

#include <trace/trace.h>

#define DEBUG(flag, format, args...) do {                          \
        __trace_write(NULL, __FILE__, __LINE__, __FUNCTION__,      \
                      (flag), NULL, format"\n", ## args);	   \
    } while (0)


extern int DBG_GRAPH, DBG_VAR, DBG_RESOLVE;



#endif /* __DRES_DEBUG_H__ */
