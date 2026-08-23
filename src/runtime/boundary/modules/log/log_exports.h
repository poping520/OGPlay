#pragma once

// name, stable module-local id, A32 word parameter count, concrete method
#define OGPLAY_LOG_BOUNDARY_EXPORTS(X)                                          \
    X("__android_log_dev_available", 0, 0, DevAvailable)                       \
    X("__android_log_write", 1, 3, Write)                                      \
    X("__android_log_buf_write", 2, 4, BufWrite)                               \
    X("__android_log_vprint", 3, 4, VPrint)                                    \
    X("__android_log_print", 4, 9, Print)                                      \
    X("__android_log_buf_print", 5, 9, BufPrint)                               \
    X("__android_log_assert", 6, 9, Assert)                                    \
    X("__android_log_bwrite", 7, 3, BinaryWrite)                               \
    X("__android_log_btwrite", 8, 4, BinaryTypedWrite)                         \
    X("android_log_format_new", 9, 0, FormatNew)                               \
    X("android_log_format_free", 10, 1, FormatFree)                            \
    X("android_log_setPrintFormat", 11, 2, SetPrintFormat)                     \
    X("android_log_formatFromString", 12, 1, FormatFromString)                 \
    X("android_log_addFilterRule", 13, 2, AddFilterRule)                       \
    X("android_log_addFilterString", 14, 2, AddFilterString)                   \
    X("android_log_shouldPrintLine", 15, 3, ShouldPrintLine)                   \
    X("android_log_processLogBuffer", 16, 2, ProcessLogBuffer)                 \
    X("android_log_processBinaryLogBuffer", 17, 5, ProcessBinaryLogBuffer)     \
    X("android_log_formatLogLine", 18, 5, FormatLogLine)                       \
    X("android_log_printLogLine", 19, 3, PrintLogLine)                         \
    X("android_openEventTagMap", 20, 1, OpenEventTagMap)                       \
    X("android_closeEventTagMap", 21, 1, CloseEventTagMap)                     \
    X("android_lookupEventTag", 22, 2, LookupEventTag)
