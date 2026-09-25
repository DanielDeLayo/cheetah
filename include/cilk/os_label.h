#ifndef _OS_LABEL_H
#define _OS_LABEL_H

#pragma GCC visibility push(default)

// Toggle which label representation to use:
#if defined(USE_OS_LABEL_STRING)
#include "os_label_string.h"
#elif defined(USE_OS_LABEL_LEB8_RANGE)
#include "os_label_leb8.h"
#elif defined(USE_OS_LABEL_LEB8_ANCHOR)
#include "os_label_leb8_anchor.h"
#else
#include "os_label_leb8_single.h"
#endif

#pragma GCC visibility pop

#endif /* _OS_LABEL_H */
