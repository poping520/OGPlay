#pragma once

// library, symbol, stable local id, A32 parameter count, concrete handler
#define OGPLAY_GUEST_SYMBOL_OVERRIDE_EXPORTS(X)                                \
    X("libc.so", "memcpy", 0, 3, Memcpy)                                    \
    X("libc.so", "memmove", 1, 3, Memmove)                                  \
    X("libc.so", "memset", 2, 3, Memset)                                    \
    X("libc.so", "memcmp", 3, 3, Memcmp)                                    \
    X("libc.so", "strlen", 4, 1, Strlen)
