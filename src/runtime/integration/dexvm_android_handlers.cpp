// android.* intrinsic handlers bound to the running guest session.
// Behaviour-sensitive gaps stay explicit failures (03 §6): unsupported
// network/SMS actions throw with accounting instead of faking success.

#include <cstring>

#include "ogplay/audio/java_sound_pool_mixer.h"
#include "ogplay/runtime/integration/dexvm_android.h"
#include "ogplay/runtime/integration/android_guest_call_session.h"

namespace ogplay::runtime {
namespace {

namespace dx = ogplay::runtime::dexvm;
using Context = std::shared_ptr<DexVmAndroidContext>;

[[nodiscard]] dx::VmValue Self(dx::IntrinsicContext& call) {
    return dx::VmValue::Ref(call.receiver);
}

[[nodiscard]] dx::VmObjectRef Singleton(dx::IntrinsicContext& call,
                                        const Context& context,
                                        const std::string& key,
                                        const char* descriptor) {
    const auto found = context->singletons.find(key);
    if (found != context->singletons.end()) return found->second;
    const auto instance = call.vm.NewIntrinsicInstance(descriptor);
    context->singletons.emplace(key, instance);
    return instance;
}

[[nodiscard]] dx::VmValue MakeString(dx::IntrinsicContext& call,
                                     const std::string& value) {
    return dx::VmValue::Ref(call.vm.NewStringUtf8(value));
}

void GuestLog(dx::IntrinsicContext& call, const core::LogLevel level,
              const std::string& line) {
    auto* logger = call.vm.Log();
    if (logger == nullptr) return;
    logger->Write(level, "runtime.dexvm.guest", line);
}

[[nodiscard]] DexVmAndroidContext::Stream& StreamOf(
    dx::IntrinsicContext& call, const Context& context) {
    const auto found = context->streams.find(call.receiver.Value());
    if (found == context->streams.end() || found->second.closed) {
        throw dx::VmJavaThrow{"Ljava/io/IOException;",
                              "stream is closed or was never opened"};
    }
    return found->second;
}

dx::VmObjectRef OpenStream(dx::IntrinsicContext& call, const Context& context,
                           std::vector<std::byte> bytes,
                           const char* descriptor = "Ljava/io/InputStream;") {
    const auto instance = call.vm.NewIntrinsicInstance(descriptor);
    context->streams[instance.Value()] =
        DexVmAndroidContext::Stream{std::move(bytes), 0, false};
    return instance;
}

[[nodiscard]] std::vector<std::byte> ReadApkFile(const Context& context,
                                                 const std::string& path) {
    return loader::ReadApkEntry(context->apk_bytes, context->archive, path);
}

void RegisterContextActivity(dx::IntrinsicRegistry& registry,
                             const Context& context) {
    registry.Register("android.context.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.activity.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.activity.lifecycle_noop",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.activity.get_window",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            Singleton(call, context, "window", "Landroid/view/Window;"));
    });
    registry.Register("android.activity.request_window_feature",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);
    });
    registry.Register("android.activity.set_content_view",
                      [context](dx::IntrinsicContext& call) {
        context->content_view = call.arguments[0].ref;
        return dx::VmValue::Void();
    });
    registry.Register("android.activity.set_volume_control_stream",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.activity.on_key_false",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);
    });
    registry.Register("android.activity.on_touch_false",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);
    });
    registry.Register("android.activity.finish",
                      [context](dx::IntrinsicContext&) {
        context->exit_requested = true;
        return dx::VmValue::Void();
    });
    registry.Register("android.context.get_package_name",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->package_name);
    });
    registry.Register("android.context.get_resources",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(
            call, context, "resources",
            "Landroid/content/res/Resources;"));
    });
    registry.Register("android.context.get_assets",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(
            call, context, "assets",
            "Landroid/content/res/AssetManager;"));
    });
    registry.Register("android.context.get_system_service",
                      [context](dx::IntrinsicContext& call) {
        const auto name = call.vm.StringUtf8(call.arguments[0].ref);
        if (name == "phone") {
            return dx::VmValue::Ref(Singleton(
                call, context, "phone",
                "Landroid/telephony/TelephonyManager;"));
        }
        if (name == "audio") {
            return dx::VmValue::Ref(Singleton(
                call, context, "audio", "Landroid/media/AudioManager;"));
        }
        if (name == "wifi") {
            return dx::VmValue::Ref(Singleton(
                call, context, "wifi", "Landroid/net/wifi/WifiManager;"));
        }
        if (name == "sensor") {
            return dx::VmValue::Ref(Singleton(
                call, context, "sensor",
                "Landroid/hardware/SensorManager;"));
        }
        throw dx::VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                              "system service is not provided: " + name};
    });
    registry.Register("android.context.register_receiver",
                      [](dx::IntrinsicContext&) {
        // Sticky broadcast lookup: nothing pending on this platform.
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.context.start_activity",
                      [](dx::IntrinsicContext&) -> dx::VmValue {
        throw dx::VmJavaThrow{
            "Ljava/lang/UnsupportedOperationException;",
            "startActivity is outside the compatibility scope"};
    });
}

void RegisterViewSurface(dx::IntrinsicRegistry& registry,
                         const Context& context) {
    registry.Register("android.window.noop", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.window.noop_add", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.window.noop_clear",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.view.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.view.noop_size", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.view.noop_focus", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.glsurfaceview.init",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.glsurfaceview.set_renderer",
                      [context](dx::IntrinsicContext& call) {
        context->renderer = call.arguments[0].ref;
        return dx::VmValue::Void();
    });
    registry.Register("android.glsurfaceview.request_render",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
}

void RegisterResources(dx::IntrinsicRegistry& registry,
                       const Context& context) {
    registry.Register("android.resources.get_configuration",
                      [context](dx::IntrinsicContext& call) {
        const auto instance = Singleton(
            call, context, "configuration",
            "Landroid/content/res/Configuration;");
        // keyboard = KEYBOARD_NOKEYS (1): desktop host has no guest keypad.
        const auto slots = call.vm.Model().InstanceSlots(instance);
        slots[0] = {1U, dx::SlotTag::cat1};
        return dx::VmValue::Ref(instance);
    });
    registry.Register("android.resources.get_identifier",
                      [context](dx::IntrinsicContext& call) {
        const auto entry_name = call.vm.StringUtf8(call.arguments[0].ref);
        const auto type_name = call.vm.StringUtf8(call.arguments[1].ref);
        const auto* entry =
            context->arsc.FindByName(type_name, entry_name);
        return dx::VmValue::Int(
            entry == nullptr ? 0
                             : static_cast<std::int32_t>(entry->resource_id));
    });
    registry.Register("android.resources.open_raw_resource",
                      [context](dx::IntrinsicContext& call) {
        const auto resource_id =
            static_cast<std::uint32_t>(call.arguments[0].AsInt());
        const auto* entry = context->arsc.FindById(resource_id);
        if (entry == nullptr || !entry->string_value.has_value()) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IllegalArgumentException;",
                "resource id has no file entry: " +
                    std::to_string(resource_id)};
        }
        return dx::VmValue::Ref(OpenStream(
            call, context, ReadApkFile(context, *entry->string_value)));
    });
    registry.Register("android.resources.get_string",
                      [](dx::IntrinsicContext&) -> dx::VmValue {
        throw dx::VmJavaThrow{
            "Ljava/lang/UnsupportedOperationException;",
            "string resources are not provided yet"};
    });
    registry.Register("android.assets.open",
                      [context](dx::IntrinsicContext& call) {
        const auto name = call.vm.StringUtf8(call.arguments[0].ref);
        return dx::VmValue::Ref(OpenStream(
            call, context, ReadApkFile(context, "assets/" + name)));
    });
    registry.Register("android.assets.open_mode",
                      [context](dx::IntrinsicContext& call) {
        const auto name = call.vm.StringUtf8(call.arguments[0].ref);
        return dx::VmValue::Ref(OpenStream(
            call, context, ReadApkFile(context, "assets/" + name)));
    });
}

void RegisterStreams(dx::IntrinsicRegistry& registry,
                     const Context& context) {
    registry.Register("android.stream.read_range",
                      [context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        const auto array = call.arguments[0].ref;
        const auto offset = call.arguments[1].AsInt();
        const auto length = call.arguments[2].AsInt();
        if (offset < 0 || length < 0) {
            throw dx::VmJavaThrow{
                "Ljava/lang/IndexOutOfBoundsException;",
                "negative stream read range"};
        }
        const auto remaining = stream.bytes.size() - stream.cursor;
        if (remaining == 0) return dx::VmValue::Int(-1);
        const auto amount = std::min<std::size_t>(
            static_cast<std::size_t>(length), remaining);
        call.vm.Model().WriteByteRegion(
            array, offset,
            std::span(stream.bytes).subspan(stream.cursor, amount));
        stream.cursor += amount;
        return dx::VmValue::Int(static_cast<std::int32_t>(amount));
    });
    registry.Register("android.stream.read_full",
                      [context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        const auto array = call.arguments[0].ref;
        const auto capacity = call.vm.Model().ArrayLength(array);
        const auto remaining = stream.bytes.size() - stream.cursor;
        if (remaining == 0) return dx::VmValue::Int(-1);
        const auto amount = std::min<std::size_t>(
            static_cast<std::size_t>(capacity), remaining);
        call.vm.Model().WriteByteRegion(
            array, 0,
            std::span(stream.bytes).subspan(stream.cursor, amount));
        stream.cursor += amount;
        return dx::VmValue::Int(static_cast<std::int32_t>(amount));
    });
    registry.Register("android.stream.available",
                      [context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        return dx::VmValue::Int(static_cast<std::int32_t>(
            stream.bytes.size() - stream.cursor));
    });
    registry.Register("android.stream.close",
                      [context](dx::IntrinsicContext& call) {
        const auto found = context->streams.find(call.receiver.Value());
        if (found != context->streams.end()) found->second.closed = true;
        return dx::VmValue::Void();
    });
    registry.Register("android.stream.skip",
                      [context](dx::IntrinsicContext& call) {
        auto& stream = StreamOf(call, context);
        const auto requested = call.arguments[0].AsLong();
        const auto remaining = static_cast<std::int64_t>(
            stream.bytes.size() - stream.cursor);
        const auto amount =
            std::max<std::int64_t>(0, std::min(requested, remaining));
        stream.cursor += static_cast<std::size_t>(amount);
        return dx::VmValue::Long(amount);
    });
}

void RegisterFiles(dx::IntrinsicRegistry& registry, const Context& context) {
    registry.Register("android.file.init",
                      [](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        slots[0] = {call.arguments[0].ref.Value(), dx::SlotTag::ref};
        return dx::VmValue::Void();
    });
    registry.Register("android.file.exists",
                      [context](dx::IntrinsicContext& call) {
        const auto slots = call.vm.Model().InstanceSlots(call.receiver);
        const auto path = call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits));
        return dx::VmValue::Int(
            context->memory_files.contains(path) ? 1 : 0);
    });
    const auto open_input = [context](dx::IntrinsicContext& call,
                                      const std::string& path) {
        const auto found = context->memory_files.find(path);
        if (found == context->memory_files.end()) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "file not found: " + path};
        }
        context->streams[call.receiver.Value()] =
            DexVmAndroidContext::Stream{found->second, 0, false};
        return dx::VmValue::Void();
    };
    registry.Register("android.file_stream.init_file",
                      [context, open_input](dx::IntrinsicContext& call) {
        const auto file = call.arguments[0].ref;
        const auto slots = call.vm.Model().InstanceSlots(file);
        return open_input(
            call, call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits)));
    });
    registry.Register("android.file_stream.init_path",
                      [context, open_input](dx::IntrinsicContext& call) {
        return open_input(call,
                          call.vm.StringUtf8(call.arguments[0].ref));
    });
    const auto open_output = [context](dx::IntrinsicContext& call,
                                       const std::string& path) {
        context->output_streams[call.receiver.Value()] =
            DexVmAndroidContext::OutputStream{path, {}, false};
        return dx::VmValue::Void();
    };
    registry.Register("android.file_output.init_path",
                      [open_output](dx::IntrinsicContext& call) {
        return open_output(call,
                           call.vm.StringUtf8(call.arguments[0].ref));
    });
    registry.Register("android.file_output.init_file",
                      [open_output](dx::IntrinsicContext& call) {
        const auto slots =
            call.vm.Model().InstanceSlots(call.arguments[0].ref);
        return open_output(
            call, call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits)));
    });
    const auto flush_output = [context](dx::IntrinsicContext& call,
                                        const std::uint32_t handle) {
        const auto found = context->output_streams.find(handle);
        if (found == context->output_streams.end()) return;
        context->memory_files[found->second.path] = found->second.bytes;
        found->second.closed = true;
        static_cast<void>(call);
    };
    registry.Register("android.file_output.close",
                      [context, flush_output](dx::IntrinsicContext& call) {
        flush_output(call, call.receiver.Value());
        return dx::VmValue::Void();
    });
    registry.Register("android.data_output.init",
                      [context](dx::IntrinsicContext& call) {
        // Chain: reuse the wrapped stream's output slot.
        const auto target = call.arguments[0].ref;
        const auto found = context->output_streams.find(target.Value());
        if (found == context->output_streams.end()) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "DataOutputStream target is not open"};
        }
        context->output_streams[call.receiver.Value()] =
            DexVmAndroidContext::OutputStream{found->second.path, {}, false};
        return dx::VmValue::Void();
    });
    registry.Register("android.data_output.write_utf",
                      [context](dx::IntrinsicContext& call) {
        auto found = context->output_streams.find(call.receiver.Value());
        if (found == context->output_streams.end() || found->second.closed) {
            throw dx::VmJavaThrow{"Ljava/io/IOException;",
                                  "DataOutputStream is closed"};
        }
        const auto text = call.vm.StringUtf8(call.arguments[0].ref);
        auto& bytes = found->second.bytes;
        bytes.push_back(static_cast<std::byte>((text.size() >> 8U) & 0xffU));
        bytes.push_back(static_cast<std::byte>(text.size() & 0xffU));
        for (const auto character : text) {
            bytes.push_back(static_cast<std::byte>(character));
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.data_output.close",
                      [context, flush_output](dx::IntrinsicContext& call) {
        flush_output(call, call.receiver.Value());
        return dx::VmValue::Void();
    });
}

void RegisterDeviceServices(dx::IntrinsicRegistry& registry,
                            const Context& context) {
    registry.Register("android.log.d", [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::debug,
                 call.vm.StringUtf8(call.arguments[0].ref) + ": " +
                     call.vm.StringUtf8(call.arguments[1].ref));
        return dx::VmValue::Int(0);
    });
    registry.Register("android.log.i", [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::info,
                 call.vm.StringUtf8(call.arguments[0].ref) + ": " +
                     call.vm.StringUtf8(call.arguments[1].ref));
        return dx::VmValue::Int(0);
    });
    registry.Register("android.log.w", [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::warn,
                 call.vm.StringUtf8(call.arguments[0].ref) + ": " +
                     call.vm.StringUtf8(call.arguments[1].ref));
        return dx::VmValue::Int(0);
    });
    registry.Register("android.log.e", [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::error,
                 call.vm.StringUtf8(call.arguments[0].ref) + ": " +
                     call.vm.StringUtf8(call.arguments[1].ref));
        return dx::VmValue::Int(0);
    });
    registry.Register("android.audio_manager.get_ringer_mode",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(2);  // RINGER_MODE_NORMAL
    });
    registry.Register("android.audio_manager.get_stream_max_volume",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(15);
    });
    registry.Register("android.audio_manager.set_stream_volume",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.wifi.is_enabled", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);
    });
    registry.Register("android.sensor.get_type", [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);  // TYPE_ACCELEROMETER
    });
    registry.Register("android.sensor_manager.get_default",
                      [](dx::IntrinsicContext&) {
        // No host sensors: games observe the documented "no sensor" result.
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.sensor_manager.register",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(0);
    });
    registry.Register("android.sensor_manager.unregister",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.telephony.get_device_id",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->device_id);
    });
    registry.Register("android.telephony.get_software_version",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->device_software_version);
    });
    registry.Register("android.telephony.get_line1_number",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->line_number);
    });
    registry.Register("android.telephony.get_network_operator",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->network_operator);
    });
    registry.Register("android.locale.get_default",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(call, context, "locale",
                                          "Ljava/util/Locale;"));
    });
    registry.Register("android.locale.get_iso3_language",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->iso3_language);
    });
    registry.Register("android.locale.get_iso3_country",
                      [context](dx::IntrinsicContext& call) {
        return MakeString(call, context->iso3_country);
    });
    registry.Register("android.thread.sleep",
                      [context](dx::IntrinsicContext& call) {
        // Unified deterministic time: sleeping advances published uptime.
        context->uptime_millis += call.arguments[0].AsLong();
        return dx::VmValue::Void();
    });
}

void RegisterAudioVideo(dx::IntrinsicRegistry& registry,
                        const Context& context) {
    registry.Register("android.sound_pool.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.sound_pool.load",
                      [context](dx::IntrinsicContext& call) {
        const auto resource = call.arguments[1].AsInt();
        auto& mixer = context->session->SoundPoolMixer();
        if (!mixer.Load(resource)) {
            GuestLog(call, core::LogLevel::warn,
                     "SoundPool.load failed for resource " +
                         std::to_string(resource));
            return dx::VmValue::Int(0);
        }
        return dx::VmValue::Int(resource);  // sound id == resource id
    });
    registry.Register("android.sound_pool.play",
                      [context](dx::IntrinsicContext& call) {
        const auto sound = call.arguments[0].AsInt();
        const auto volume = call.arguments[1].AsFloat();
        const auto loop = call.arguments[3].AsInt();
        auto& mixer = context->session->SoundPoolMixer();
        const auto stream = context->next_sound_stream++;
        if (!mixer.Play(audio::JavaSoundPoolKind::pool, sound, stream,
                        volume, loop != 0)) {
            return dx::VmValue::Int(0);
        }
        context->sound_streams[stream] = sound;
        return dx::VmValue::Int(stream);
    });
    const auto stream_call =
        [context](dx::IntrinsicContext& call,
                  const std::function<void(audio::JavaSoundPoolMixer&,
                                           std::int32_t, std::int32_t)>&
                      action) {
            const auto stream = call.arguments[0].AsInt();
            const auto found = context->sound_streams.find(stream);
            if (found != context->sound_streams.end()) {
                action(context->session->SoundPoolMixer(), found->second,
                       stream);
            }
            return dx::VmValue::Void();
        };
    registry.Register("android.sound_pool.pause",
                      [stream_call](dx::IntrinsicContext& call) {
        return stream_call(call, [](auto& mixer, const auto resource,
                                    const auto stream) {
            mixer.Pause(audio::JavaSoundPoolKind::pool, resource, stream);
        });
    });
    registry.Register("android.sound_pool.resume",
                      [stream_call](dx::IntrinsicContext& call) {
        return stream_call(call, [](auto& mixer, const auto resource,
                                    const auto stream) {
            mixer.Resume(audio::JavaSoundPoolKind::pool, resource, stream);
        });
    });
    registry.Register("android.sound_pool.stop",
                      [stream_call](dx::IntrinsicContext& call) {
        return stream_call(call, [](auto& mixer, const auto resource,
                                    const auto stream) {
            mixer.Stop(audio::JavaSoundPoolKind::pool, resource, stream);
        });
    });
    registry.Register("android.sound_pool.set_volume",
                      [context](dx::IntrinsicContext& call) {
        const auto stream = call.arguments[0].AsInt();
        const auto found = context->sound_streams.find(stream);
        if (found != context->sound_streams.end()) {
            context->session->SoundPoolMixer().SetVolume(
                audio::JavaSoundPoolKind::pool, found->second, stream,
                call.arguments[1].AsFloat());
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.sound_pool.set_rate",
                      [context](dx::IntrinsicContext& call) {
        const auto stream = call.arguments[0].AsInt();
        const auto found = context->sound_streams.find(stream);
        if (found != context->sound_streams.end()) {
            context->session->SoundPoolMixer().SetPitch(
                audio::JavaSoundPoolKind::pool, found->second, stream,
                call.arguments[1].AsFloat());
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.sound_pool.unload",
                      [context](dx::IntrinsicContext& call) {
        context->session->SoundPoolMixer().Unload(
            call.arguments[0].AsInt());
        return dx::VmValue::Int(1);
    });
    registry.Register("android.sound_pool.release",
                      [context](dx::IntrinsicContext&) {
        context->session->SoundPoolMixer().StopAllSounds();
        return dx::VmValue::Void();
    });

    registry.Register("android.media_player.create",
                      [context](dx::IntrinsicContext& call) {
        const auto resource = call.arguments[1].AsInt();
        const auto instance = call.vm.NewIntrinsicInstance(
            "Landroid/media/MediaPlayer;");
        if (!context->session->SoundPoolMixer().Load(resource)) {
            GuestLog(call, core::LogLevel::warn,
                     "MediaPlayer.create failed for resource " +
                         std::to_string(resource));
            return dx::VmValue::Ref(dx::VmObjectRef{});
        }
        context->media_resources[instance.Value()] = resource;
        context->media_playing[instance.Value()] = false;
        return dx::VmValue::Ref(instance);
    });
    const auto media_resource = [context](dx::IntrinsicContext& call)
        -> std::optional<std::int32_t> {
        const auto found =
            context->media_resources.find(call.receiver.Value());
        if (found == context->media_resources.end()) return std::nullopt;
        return found->second;
    };
    registry.Register("android.media_player.start",
                      [context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            static_cast<void>(context->session->SoundPoolMixer().Play(
                audio::JavaSoundPoolKind::big, *resource, 0, 1.0F, true));
            context->media_playing[call.receiver.Value()] = true;
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.pause",
                      [context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            context->session->SoundPoolMixer().Pause(
                audio::JavaSoundPoolKind::big, *resource, 0);
            context->media_playing[call.receiver.Value()] = false;
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.stop",
                      [context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            context->session->SoundPoolMixer().Stop(
                audio::JavaSoundPoolKind::big, *resource, 0);
            context->media_playing[call.receiver.Value()] = false;
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.release",
                      [context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            context->session->SoundPoolMixer().Stop(
                audio::JavaSoundPoolKind::big, *resource, 0);
            context->session->SoundPoolMixer().Unload(*resource);
        }
        context->media_resources.erase(call.receiver.Value());
        context->media_playing.erase(call.receiver.Value());
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.is_playing",
                      [context](dx::IntrinsicContext& call) {
        const auto found =
            context->media_playing.find(call.receiver.Value());
        return dx::VmValue::Int(
            found != context->media_playing.end() && found->second ? 1 : 0);
    });
    registry.Register("android.media_player.prepare",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.seek_to",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.set_looping",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.set_volume",
                      [context, media_resource](dx::IntrinsicContext& call) {
        const auto resource = media_resource(call);
        if (resource.has_value()) {
            context->session->SoundPoolMixer().SetVolume(
                audio::JavaSoundPoolKind::big, *resource, 0,
                call.arguments[0].AsFloat());
        }
        return dx::VmValue::Void();
    });
    registry.Register("android.media_player.set_completion_listener",
                      [](dx::IntrinsicContext&) {
        // Completion callbacks require the media clock; recorded gap.
        return dx::VmValue::Void();
    });
}

void RegisterMisc(dx::IntrinsicRegistry& registry, const Context& context) {
    registry.Register("android.bundle.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.bundle.get", [](dx::IntrinsicContext&) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.receiver.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.receiver.on_receive_noop",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.intent_filter.init",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.intent_filter.add_action",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.intent.init", [](dx::IntrinsicContext&) {
        return dx::VmValue::Void();
    });
    registry.Register("android.intent.get_action",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.intent.get_extras",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.intent.set_flags",
                      [](dx::IntrinsicContext& call) {
        return Self(call);
    });
    registry.Register("android.intent.set_data_and_type",
                      [](dx::IntrinsicContext& call) {
        return Self(call);
    });
    registry.Register("android.pending_intent.get_broadcast",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Ref(dx::VmObjectRef{});
    });
    registry.Register("android.uri.parse", [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Landroid/net/Uri;"));
    });
    registry.Register("android.toast.make_text",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(
            call.vm.NewIntrinsicInstance("Landroid/widget/Toast;"));
    });
    registry.Register("android.toast.show", [](dx::IntrinsicContext& call) {
        GuestLog(call, core::LogLevel::info, "Toast.show()");
        return dx::VmValue::Void();
    });
    registry.Register("android.sms.get_default",
                      [context](dx::IntrinsicContext& call) {
        return dx::VmValue::Ref(Singleton(
            call, context, "sms", "Landroid/telephony/SmsManager;"));
    });
    for (const auto* blocked :
         {"android.sms.send_text", "android.sms.create_from_pdu",
          "android.sms.get_message_body",
          "android.sms.get_originating_address",
          "android.net.unsupported"}) {
        registry.Register(blocked, [](dx::IntrinsicContext&) -> dx::VmValue {
            throw dx::VmJavaThrow{
                "Ljava/lang/UnsupportedOperationException;",
                "SMS/network actions are outside the compatibility scope"};
        });
    }

    // Motion events read their slots directly.
    registry.Register("android.motion_event.get_action",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            call.vm.Model().InstanceSlots(call.receiver)[0].bits));
    });
    const auto slot_float = [](dx::IntrinsicContext& call,
                               const std::size_t slot) {
        dx::VmValue value;
        value.kind = dx::VmValue::Kind::cat1;
        value.cat1 = call.vm.Model().InstanceSlots(call.receiver)[slot].bits;
        return value;
    };
    registry.Register("android.motion_event.get_x",
                      [slot_float](dx::IntrinsicContext& call) {
        return slot_float(call, 1);
    });
    registry.Register("android.motion_event.get_y",
                      [slot_float](dx::IntrinsicContext& call) {
        return slot_float(call, 2);
    });
    registry.Register("android.motion_event.get_x_indexed",
                      [slot_float](dx::IntrinsicContext& call) {
        return slot_float(call, 1);
    });
    registry.Register("android.motion_event.get_y_indexed",
                      [slot_float](dx::IntrinsicContext& call) {
        return slot_float(call, 2);
    });
    registry.Register("android.motion_event.get_pointer_count",
                      [](dx::IntrinsicContext&) {
        return dx::VmValue::Int(1);
    });
    registry.Register("android.motion_event.get_pointer_id",
                      [](dx::IntrinsicContext& call) {
        return dx::VmValue::Int(static_cast<std::int32_t>(
            call.vm.Model().InstanceSlots(call.receiver)[3].bits));
    });

    // Platform System handlers (declared by the core catalog).
    registry.Register("platform.system.current_time_millis",
                      [context](dx::IntrinsicContext&) {
        // Deterministic epoch base plus lifecycle-published uptime.
        return dx::VmValue::Long(1'400'000'000'000LL +
                                 context->uptime_millis.load());
    });
    registry.Register("platform.system.nano_time",
                      [context](dx::IntrinsicContext&) {
        return dx::VmValue::Long(context->uptime_millis.load() * 1'000'000LL);
    });
    registry.Register("platform.system.load_library",
                      [](dx::IntrinsicContext& call) {
        // Libraries are preloaded and initialized by the session
        // (04 §2 step 4); the name is recorded for diagnostics.
        GuestLog(call, core::LogLevel::info,
                 "System.loadLibrary(" +
                     call.vm.StringUtf8(call.arguments[0].ref) + ")");
        return dx::VmValue::Void();
    });
    registry.Register("platform.system.exit",
                      [context](dx::IntrinsicContext&) {
        context->exit_requested = true;
        return dx::VmValue::Void();
    });
}

}  // namespace

void RegisterAndroidBuiltins(dx::IntrinsicRegistry& registry,
                             const std::shared_ptr<DexVmAndroidContext>
                                 context) {
    RegisterContextActivity(registry, context);
    RegisterViewSurface(registry, context);
    RegisterResources(registry, context);
    RegisterStreams(registry, context);
    RegisterFiles(registry, context);
    RegisterDeviceServices(registry, context);
    RegisterAudioVideo(registry, context);
    RegisterMisc(registry, context);
}

dx::VmObjectRef MakeMotionEvent(dx::Interpreter& vm,
                                const std::int32_t action, const float x,
                                const float y, const std::int32_t pointer) {
    const auto instance =
        vm.NewIntrinsicInstance("Landroid/view/MotionEvent;");
    const auto slots = vm.Model().InstanceSlots(instance);
    std::uint32_t x_bits{};
    std::uint32_t y_bits{};
    std::memcpy(&x_bits, &x, sizeof(x_bits));
    std::memcpy(&y_bits, &y, sizeof(y_bits));
    slots[0] = {static_cast<std::uint32_t>(action), dx::SlotTag::cat1};
    slots[1] = {x_bits, dx::SlotTag::cat1};
    slots[2] = {y_bits, dx::SlotTag::cat1};
    slots[3] = {static_cast<std::uint32_t>(pointer), dx::SlotTag::cat1};
    return instance;
}

}  // namespace ogplay::runtime
