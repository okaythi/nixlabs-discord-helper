#pragma once
#if defined(__has_include)
#if __has_include(<eventtoken.h>)
#include <eventtoken.h>
#else
typedef struct EventRegistrationToken {
    __int64 value;
} EventRegistrationToken;
#endif
#else
#include <eventtoken.h>
#endif
