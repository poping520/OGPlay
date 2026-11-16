#include "probe_support.h"

#include <dlfcn.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <typeinfo>

// Recent static libc++ uses the Android abort annotation API, absent from KitKat.
// Preserve the message on older libcs as well. libc++ still performs the abort.
extern "C" void __wrap_android_set_abort_message(const char* message) {
    auto set_message = reinterpret_cast<void (*)(const char*)>(dlsym(RTLD_DEFAULT, "android_set_abort_message"));
    if (set_message != nullptr) {
        set_message(message);
    } else if (message != nullptr) {
        (void)write(STDERR_FILENO, message, std::strlen(message));
        (void)write(STDERR_FILENO, "\n", 1);
    }
}

namespace {
struct Event {
    virtual ~Event() {}
};

struct Marker final : Event {
    int code = 19;
};

// Keep the dynamic type through the optimized build.
__attribute__((noinline)) Event* select_event(Event* plain, Event* marker, bool use_marker) {
    return use_marker ? marker : plain;
}

__attribute__((noinline)) void throw_marker(int code) {
    throw code;
}
}  // namespace

extern "C" JNIEXPORT jlongArray JNICALL Java_org_ogplay_probe_NativeProbe_cppRuntime(JNIEnv* env, jclass,
                                                                                     jboolean flag) {
    Event plain;
    Marker marker;
    Event* value = select_event(&plain, &marker, flag);
    Marker* cast = dynamic_cast<Marker*>(value);
    int caught = 0;
    try {
        throw_marker(cast == nullptr ? 0 : cast->code);
    } catch (int error) {
        caught = error;
    }
    std::atomic<int> count(19);
    int previous = count.fetch_add(3, std::memory_order_relaxed);
    int expected = 22;
    bool swapped = count.compare_exchange_strong(expected, 40, std::memory_order_seq_cst);
    jlong result[] = {cast != nullptr, typeid(*value) == typeid(Marker), caught, previous, swapped, count.load()};
    return probe_longs(env, result, sizeof(result) / sizeof(result[0]));
}
