#include "probe_support.h"

#include <SLES/OpenSLES.h>
#include <SLES/OpenSLES_Android.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/looper.h>
#include <android/sensor.h>
#include <errno.h>
#include <stdatomic.h>
#include <unistd.h>

#define COUNT(values) (sizeof(values) / sizeof((values)[0]))

JNIEXPORT jlongArray JNICALL Java_org_ogplay_probe_NativeProbe_asset(JNIEnv* env, jclass type, jobject manager) {
    (void)type;
    AAssetManager* assets = AAssetManager_fromJava(env, manager);
    AAsset* asset = assets == NULL ? NULL : AAssetManager_open(assets, "fixtures/deflated.bin", AASSET_MODE_STREAMING);
    if (asset == NULL) {
        jlong failed[] = {0, 0, assets == NULL ? -1 : -2};
        return probe_longs(env, failed, COUNT(failed));
    }
    uint32_t hash = UINT32_C(2166136261);
    unsigned char bytes[64];
    int read_count;
    size_t total = 0;
    while ((read_count = AAsset_read(asset, bytes, sizeof(bytes))) > 0 && total < 32768) {
        for (int i = 0; i < read_count; i++)
            hash = (hash ^ bytes[i]) * UINT32_C(16777619);
        total += (size_t)read_count;
    }
    jlong result[] = {(jlong)total, hash, read_count};
    AAsset_close(asset);
    return probe_longs(env, result, COUNT(result));
}

struct PipeResult {
    int callbacks;
    int value;
};

static int on_pipe(int fd, int events, void* user) {
    struct PipeResult* result = user;
    ++result->callbacks;
    unsigned char value;
    if ((events & ALOOPER_EVENT_INPUT) != 0 && read(fd, &value, 1) == 1) result->value = value;
    return 0;
}

JNIEXPORT jlongArray JNICALL Java_org_ogplay_probe_NativeProbe_looper(JNIEnv* env, jclass type) {
    (void)type;
    int descriptors[2];
    struct PipeResult result = {.callbacks = 0, .value = -1};
    jlong stats[] = {0, -1, -1, -1, 0, -1, 0, -1};
    ALooper* loop = ALooper_prepare(0);
    stats[0] = loop != NULL;
    if (loop && pipe(descriptors) == 0) {
        stats[1] = 0;
        stats[2] = ALooper_addFd(loop, descriptors[0], ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, on_pipe, &result);
        if (stats[2] == 1) {
            const unsigned char value = 19;
            stats[3] = write(descriptors[1], &value, 1);
            if (stats[3] == 1) stats[6] = ALooper_pollOnce(350, NULL, NULL, NULL);
            stats[7] = ALooper_removeFd(loop, descriptors[0]);
        }
        close(descriptors[1]);
        close(descriptors[0]);
    } else if (loop) {
        stats[1] = errno;
    }
    stats[4] = result.callbacks;
    stats[5] = result.value;
    return probe_longs(env, stats, COUNT(stats));
}

JNIEXPORT jlongArray JNICALL Java_org_ogplay_probe_NativeProbe_sensor(JNIEnv* env, jclass type) {
    (void)type;
    jlong result[] = {0, 0, -1, -1, 0, 0, -1, 0, -1};
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    ASensorManager* manager = ASensorManager_getInstance();
#pragma clang diagnostic pop
    if (manager == NULL) return probe_longs(env, result, COUNT(result));
    ASensorList list = NULL;
    result[0] = ASensorManager_getSensorList(manager, &list);
    const ASensor* sensor = ASensorManager_getDefaultSensor(manager, ASENSOR_TYPE_ACCELEROMETER);
    if (sensor == NULL || list == NULL) return probe_longs(env, result, COUNT(result));
    for (jlong i = 0; i < result[0]; i++)
        result[1] |= list[i] == sensor;
    ALooper* looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
    if (looper == NULL) return probe_longs(env, result, COUNT(result));
    const int ident = 27;
    ASensorEventQueue* queue = ASensorManager_createEventQueue(manager, looper, ident, NULL, NULL);
    if (queue == NULL) return probe_longs(env, result, COUNT(result));
    result[2] = ASensorEventQueue_enableSensor(queue, sensor);
    result[3] = ASensorEventQueue_setEventRate(queue, sensor, 20000);
    int events = 0;
    result[4] = ALooper_pollOnce(350, NULL, &events, NULL) == ident && (events & ALOOPER_EVENT_INPUT);
    ASensorEvent samples[16];
    ssize_t count = ASensorEventQueue_getEvents(queue, samples, COUNT(samples));
    if (count > 0) {
        ASensorEvent* sample = &samples[0];
        const float x = sample->acceleration.x, y = sample->acceleration.y, z = sample->acceleration.z;
        const float squared = x * x + y * y + z * z;
        result[5] = sample->timestamp > 0 && sample->type == ASENSOR_TYPE_ACCELEROMETER && squared >= 9.66f * 9.66f &&
                    squared <= 9.96f * 9.96f;
    }
    result[6] = ASensorEventQueue_disableSensor(queue, sensor);
    while (ASensorEventQueue_getEvents(queue, samples, COUNT(samples)) > 0) {
    }
    result[7] = ALooper_pollOnce(100, NULL, NULL, NULL) == ALOOPER_POLL_TIMEOUT;
    result[8] = ASensorManager_destroyEventQueue(manager, queue);
    return probe_longs(env, result, COUNT(result));
}

static void on_buffer(SLAndroidSimpleBufferQueueItf queue, void* user) {
    (void)queue;
    atomic_uint* done = user;
    atomic_fetch_add_explicit(done, 1, memory_order_relaxed);
}

JNIEXPORT jlongArray JNICALL Java_org_ogplay_probe_NativeProbe_openSl(JNIEnv* env, jclass type) {
    (void)type;
    SLObjectItf engine_object = NULL, output = NULL, player = NULL;
    SLEngineItf engine = NULL;
    SLPlayItf play = NULL;
    SLAndroidSimpleBufferQueueItf queue = NULL;
    atomic_uint consumed;
    atomic_init(&consumed, 0);
    static const int16_t pcm[3][480] = {{19, -19, 29, -29}, {29, -29, 19, -19}, {39, -39, 19, -19}};
    SLDataLocator_AndroidSimpleBufferQueue input_location = {SL_DATALOCATOR_ANDROIDSIMPLEBUFFERQUEUE, 3};
    SLDataFormat_PCM format = {SL_DATAFORMAT_PCM,           1,
                               SL_SAMPLINGRATE_8,           SL_PCMSAMPLEFORMAT_FIXED_16,
                               SL_PCMSAMPLEFORMAT_FIXED_16, SL_SPEAKER_FRONT_CENTER,
                               SL_BYTEORDER_LITTLEENDIAN};
    SLDataSource source = {&input_location, &format};
    SLDataLocator_OutputMix location = {SL_DATALOCATOR_OUTPUTMIX, NULL};
    SLDataSink destination = {&location, NULL};
    const SLInterfaceID interfaces[] = {SL_IID_ANDROIDSIMPLEBUFFERQUEUE};
    const SLboolean required[] = {SL_BOOLEAN_TRUE};

    SLresult status;
    int stage = 0;
    int queued = 0;
#define DO_SL(operation)                                                                                               \
    do {                                                                                                               \
        stage++;                                                                                                       \
        status = (operation);                                                                                          \
        if (status) goto destroy;                                                                                      \
    } while (0)
    DO_SL(slCreateEngine(&engine_object, 0, NULL, 0, NULL, NULL));
    DO_SL((*engine_object)->Realize(engine_object, SL_BOOLEAN_FALSE));
    DO_SL((*engine_object)->GetInterface(engine_object, SL_IID_ENGINE, &engine));
    DO_SL((*engine)->CreateOutputMix(engine, &output, 0, NULL, NULL));
    DO_SL((*output)->Realize(output, SL_BOOLEAN_FALSE));
    location.outputMix = output;
    DO_SL((*engine)->CreateAudioPlayer(engine, &player, &source, &destination, 1, interfaces, required));
    DO_SL((*player)->Realize(player, SL_BOOLEAN_FALSE));
    DO_SL((*player)->GetInterface(player, SL_IID_PLAY, &play));
    DO_SL((*player)->GetInterface(player, SL_IID_ANDROIDSIMPLEBUFFERQUEUE, &queue));
    DO_SL((*queue)->RegisterCallback(queue, on_buffer, &consumed));
    for (; queued < 3; queued++)
        DO_SL((*queue)->Enqueue(queue, pcm[queued], sizeof(pcm[queued])));
    DO_SL((*play)->SetPlayState(play, SL_PLAYSTATE_PLAYING));

    for (int interval = 0; interval < 100 && atomic_load(&consumed) < 3; interval++) {
        const struct timespec delay = {0, 10000000};
        if (nanosleep(&delay, NULL) != 0) break;
    }
    DO_SL((*play)->SetPlayState(play, SL_PLAYSTATE_STOPPED));
#undef DO_SL

destroy:
    if (player != NULL) (*player)->Destroy(player);
    if (output != NULL) (*output)->Destroy(output);
    if (engine_object != NULL) (*engine_object)->Destroy(engine_object);
    jlong result[] = {stage, status, queued, atomic_load(&consumed)};
    return probe_longs(env, result, COUNT(result));
}
