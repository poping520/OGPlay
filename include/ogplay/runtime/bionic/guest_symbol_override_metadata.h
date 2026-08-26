#pragma once

// library, symbol, stable local id, A32 parameter count, concrete handler
#define OGPLAY_LIBC_GUEST_SYMBOL_OVERRIDE_EXPORTS(X)                           \
    X("libc.so", "memcpy", 0, 3, Memcpy)                                    \
    X("libc.so", "memmove", 1, 3, Memmove)                                  \
    X("libc.so", "memset", 2, 3, Memset)                                    \
    X("libc.so", "memcmp", 3, 3, Memcmp)                                    \
    X("libc.so", "strlen", 4, 1, Strlen)

#define OGPLAY_LIBDL_GUEST_SYMBOL_OVERRIDE_EXPORTS(X)                         \
    X("libdl.so", "dlopen", 0, 2, Dlopen)                                   \
    X("libdl.so", "dlsym", 1, 2, Dlsym)                                     \
    X("libdl.so", "dlclose", 2, 1, Dlclose)                                 \
    X("libdl.so", "dlerror", 3, 0, Dlerror)

#define OGPLAY_GUEST_SYMBOL_OVERRIDE_EXPORTS(X)                               \
    OGPLAY_LIBC_GUEST_SYMBOL_OVERRIDE_EXPORTS(X)                              \
    OGPLAY_LIBDL_GUEST_SYMBOL_OVERRIDE_EXPORTS(X)
