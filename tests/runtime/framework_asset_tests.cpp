#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <doctest/doctest.h>

#include "ogplay/runtime/framework_asset.h"
#include "ogplay/runtime/framework_lifecycle.h"

namespace {

[[nodiscard]] ogplay::runtime::JniMethodId Method(
    const ogplay::runtime::JniClassRegistry& classes,
    const ogplay::runtime::JniObjectIdentity type, const char* name,
    const char* descriptor) {
    return *classes.GetMethodId(type, name, descriptor, false);
}

[[nodiscard]] ogplay::runtime::JniReference ReferenceResult(
    const ogplay::runtime::JniValue& value) {
    return std::get<ogplay::runtime::JniReference>(value);
}

}  // namespace

TEST_CASE("framework AssetManager streams APK assets through JNI arrays") {
    ogplay::runtime::VirtualFileSystem vfs;
    const std::vector<ogplay::runtime::VfsMountEntry> apk{{
        "assets/data.bin",
        {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4},
         std::byte{5}},
    }};
    vfs.Mount(ogplay::runtime::VfsSource::apk, "/apk", apk);

    ogplay::runtime::JniClassRegistry classes;
    ogplay::runtime::JniInvocationEngine invocations(classes);
    ogplay::runtime::JniEnvironment environment;
    ogplay::runtime::JniStringStore strings;
    ogplay::runtime::JniPrimitiveArrayStore arrays;
    ogplay::runtime::FrameworkLifecycleHle lifecycle(classes, invocations);
    const auto framework = lifecycle.Install();
    ogplay::runtime::FrameworkAssetHle assets(
        classes, invocations, environment, strings, arrays, vfs);
    const auto asset_classes = assets.Install();

    constexpr std::uint64_t thread = 41;
    environment.AttachThread(thread, 16);
    const auto activity = environment.PublishLocalObject(
        thread, ogplay::runtime::AllocateJniHostObjectIdentity());
    const std::vector<ogplay::runtime::JniChar> asset_name{
        'd', 'a', 't', 'a', '.', 'b', 'i', 'n'};
    const auto name_object = strings.Create(asset_name);
    const auto name = environment.PublishLocalObject(thread, name_object);
    const auto bytes_object =
        arrays.New(ogplay::runtime::JniPrimitiveKind::byte, 4);
    const auto bytes = environment.PublishLocalObject(thread, bytes_object);

    const auto manager = ReferenceResult(invocations.InvokeVirtual(
        thread, activity, framework.activity_class,
        Method(classes, framework.activity_class, "getAssets",
               "()Landroid/content/res/AssetManager;"),
        {}, ogplay::runtime::JniArgumentSource::value_array));
    const std::array<ogplay::runtime::JniValue, 1> open_arguments{name};
    const auto stream = ReferenceResult(invocations.InvokeVirtual(
        thread, manager, asset_classes.asset_manager_class,
        Method(classes, asset_classes.asset_manager_class, "open",
               "(Ljava/lang/String;)Ljava/io/InputStream;"),
        open_arguments, ogplay::runtime::JniArgumentSource::value_array));

    const auto available = Method(classes, asset_classes.input_stream_class,
                                  "available", "()I");
    const auto read_one = Method(classes, asset_classes.input_stream_class,
                                 "read", "()I");
    const auto read = Method(classes, asset_classes.input_stream_class, "read",
                             "([B)I");
    const auto read_range = Method(classes, asset_classes.input_stream_class,
                                   "read", "([BII)I");
    const auto close = Method(classes, asset_classes.input_stream_class,
                              "close", "()V");
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeVirtual(
              thread, stream, asset_classes.input_stream_class, available, {},
              ogplay::runtime::JniArgumentSource::value_array)) == 5);
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeVirtual(
              thread, stream, asset_classes.input_stream_class, read_one, {},
              ogplay::runtime::JniArgumentSource::value_array)) == 1);
    const auto skip = Method(classes, asset_classes.input_stream_class, "skip",
                             "(J)J");
    const std::array<ogplay::runtime::JniValue, 1> negative_skip{
        ogplay::runtime::JniLong{-1}};
    CHECK(std::get<ogplay::runtime::JniLong>(invocations.InvokeVirtual(
              thread, stream, asset_classes.input_stream_class, skip,
              negative_skip,
              ogplay::runtime::JniArgumentSource::value_array)) == 0);
    const std::array<ogplay::runtime::JniValue, 1> skip_one{
        ogplay::runtime::JniLong{1}};
    CHECK(std::get<ogplay::runtime::JniLong>(invocations.InvokeVirtual(
              thread, stream, asset_classes.input_stream_class, skip, skip_one,
              ogplay::runtime::JniArgumentSource::value_array)) == 1);
    const std::array<ogplay::runtime::JniValue, 1> read_arguments{bytes};
    const std::array<ogplay::runtime::JniValue, 3> range_arguments{
        bytes, ogplay::runtime::JniInt{1}, ogplay::runtime::JniInt{2}};
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeVirtual(
              thread, stream, asset_classes.input_stream_class, read_range,
              range_arguments,
              ogplay::runtime::JniArgumentSource::value_array)) == 2);
    const std::vector<ogplay::runtime::JniByte> first{0, 3, 4, 0};
    CHECK(std::get<std::vector<ogplay::runtime::JniByte>>(
              arrays.Region(bytes_object, 0, 4)) == first);
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeVirtual(
              thread, stream, asset_classes.input_stream_class, available, {},
              ogplay::runtime::JniArgumentSource::value_array)) == 1);
    const std::array<ogplay::runtime::JniValue, 3> invalid_range{
        bytes, ogplay::runtime::JniInt{3}, ogplay::runtime::JniInt{2}};
    CHECK_THROWS_AS(
        static_cast<void>(invocations.InvokeVirtual(
            thread, stream, asset_classes.input_stream_class, read_range,
            invalid_range,
            ogplay::runtime::JniArgumentSource::value_array)),
        ogplay::runtime::FrameworkAssetError);
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeVirtual(
              thread, stream, asset_classes.input_stream_class, available, {},
              ogplay::runtime::JniArgumentSource::value_array)) == 1);
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeVirtual(
              thread, stream, asset_classes.input_stream_class, read,
              read_arguments,
              ogplay::runtime::JniArgumentSource::value_array)) == 1);
    CHECK(std::get<std::vector<ogplay::runtime::JniByte>>(
              arrays.Region(bytes_object, 0, 1)) ==
          std::vector<ogplay::runtime::JniByte>{5});
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeVirtual(
              thread, stream, asset_classes.input_stream_class, read,
              read_arguments,
              ogplay::runtime::JniArgumentSource::value_array)) == -1);
    CHECK(std::get<ogplay::runtime::JniInt>(invocations.InvokeVirtual(
              thread, stream, asset_classes.input_stream_class, read_one, {},
              ogplay::runtime::JniArgumentSource::value_array)) == -1);

    static_cast<void>(invocations.InvokeVirtual(
        thread, stream, asset_classes.input_stream_class, close, {},
        ogplay::runtime::JniArgumentSource::value_array));
    static_cast<void>(invocations.InvokeVirtual(
        thread, stream, asset_classes.input_stream_class, close, {},
        ogplay::runtime::JniArgumentSource::value_array));
    CHECK_THROWS_AS(
        static_cast<void>(invocations.InvokeVirtual(
            thread, stream, asset_classes.input_stream_class, read,
            read_arguments,
            ogplay::runtime::JniArgumentSource::value_array)),
        ogplay::runtime::FrameworkAssetError);

    environment.DetachThread(thread);
    arrays.Delete(bytes_object);
    strings.Delete(name_object);
}

TEST_CASE("framework AssetManager rejects unsafe paths and missing setup") {
    ogplay::runtime::JniClassRegistry missing_classes;
    ogplay::runtime::JniInvocationEngine missing_invocations(missing_classes);
    ogplay::runtime::JniEnvironment environment;
    ogplay::runtime::JniStringStore strings;
    ogplay::runtime::JniPrimitiveArrayStore arrays;
    ogplay::runtime::VirtualFileSystem vfs;
    const std::array runtime_asset{std::byte{9}};
    vfs.PutFile("/apk/assets/runtime.bin", runtime_asset, false);
    ogplay::runtime::FrameworkAssetHle missing(
        missing_classes, missing_invocations, environment, strings, arrays,
        vfs);
    CHECK_THROWS_AS(static_cast<void>(missing.Install()),
                    ogplay::runtime::FrameworkAssetError);

    ogplay::runtime::JniClassRegistry classes;
    ogplay::runtime::JniInvocationEngine invocations(classes);
    ogplay::runtime::FrameworkLifecycleHle lifecycle(classes, invocations);
    const auto framework = lifecycle.Install();
    ogplay::runtime::FrameworkAssetHle assets(
        classes, invocations, environment, strings, arrays, vfs);
    const auto installed = assets.Install();
    CHECK_THROWS_AS(static_cast<void>(assets.Install()),
                    ogplay::runtime::FrameworkAssetError);

    constexpr std::uint64_t thread = 42;
    environment.AttachThread(thread, 4);
    const auto activity = environment.PublishLocalObject(
        thread, ogplay::runtime::AllocateJniHostObjectIdentity());
    const auto manager = ReferenceResult(invocations.InvokeVirtual(
        thread, activity, framework.activity_class,
        Method(classes, framework.activity_class, "getAssets",
               "()Landroid/content/res/AssetManager;"),
        {}, ogplay::runtime::JniArgumentSource::value_array));
    const std::vector<ogplay::runtime::JniChar> unsafe_name{
        '.', '.', '/', 'e', 's', 'c', 'a', 'p', 'e'};
    const auto name_object = strings.Create(unsafe_name);
    const auto name = environment.PublishLocalObject(thread, name_object);
    const std::array<ogplay::runtime::JniValue, 1> arguments{name};
    CHECK_THROWS_AS(
        static_cast<void>(invocations.InvokeVirtual(
            thread, manager, installed.asset_manager_class,
            Method(classes, installed.asset_manager_class, "open",
                   "(Ljava/lang/String;)Ljava/io/InputStream;"),
            arguments, ogplay::runtime::JniArgumentSource::value_array)),
        ogplay::runtime::FrameworkAssetError);
    const std::vector<ogplay::runtime::JniChar> runtime_name{
        'r', 'u', 'n', 't', 'i', 'm', 'e', '.', 'b', 'i', 'n'};
    const auto runtime_name_object = strings.Create(runtime_name);
    const auto runtime_name_reference =
        environment.PublishLocalObject(thread, runtime_name_object);
    const std::array<ogplay::runtime::JniValue, 1> runtime_arguments{
        runtime_name_reference};
    CHECK_THROWS_AS(
        static_cast<void>(invocations.InvokeVirtual(
            thread, manager, installed.asset_manager_class,
            Method(classes, installed.asset_manager_class, "open",
                   "(Ljava/lang/String;)Ljava/io/InputStream;"),
            runtime_arguments,
            ogplay::runtime::JniArgumentSource::value_array)),
        ogplay::runtime::FrameworkAssetError);
    environment.DetachThread(thread);
    strings.Delete(runtime_name_object);
    strings.Delete(name_object);
}
