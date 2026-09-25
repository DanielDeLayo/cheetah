#include <cilk/os_label.h>

#if defined(_OS_LABEL_LEB8_SINGLE_H)
#include "os_label_leb8_single.cpp"
#elif defined(_OS_LABEL_LEB8_ANCHOR_H)
#include "os_label_leb8_anchor.cpp"
#elif defined(_OS_LABEL_LEB8_H)
#include "os_label_leb8.cpp"
#elif defined(_OS_LABEL_STRING_H)
#include "os_label_string.cpp"
#else
#error "Unknown OS label representation"
#endif
