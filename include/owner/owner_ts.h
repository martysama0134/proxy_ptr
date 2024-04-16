#ifndef INCLUDE_OWNER_OWNER_TS_H
#define INCLUDE_OWNER_OWNER_TS_H

#ifdef INCLUDE_OWNER_OWNER_H
    #error "YOU CANNOT INCLUDE owner_ts.h IF YOU HAVE INCLUDED owner.h"
#endif

#define PROXY_THREAD_SAFE
#include "owner.h"

#endif  // INCLUDE_OWNER_OWNER_TS_H
