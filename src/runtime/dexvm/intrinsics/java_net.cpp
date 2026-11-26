// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_net_MalformedURLException.cpp ----
#include "catalog.h"
#include "ogplay/runtime/dexvm/nio_buffer_access.h"
#include "ogplay/runtime/dexvm/guest_memory.h"
#include "shared.h"
#include "api19_os_constants.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"

// ---- migrated from dexvm_android java.net / javax.net.ssl ----

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string_view>

#include <boost/url/encode.hpp>
#include <boost/url/grammar/lut_chars.hpp>
#include <boost/url/pct_string_view.hpp>

#include "ogplay/core/text.h"
#include "ogplay/runtime/dexvm/io_runtime.h"
#include "ogplay/runtime/dexvm/network_runtime.h"
#include "ogplay/runtime/net/guest_network.h"
#include "ogplay/runtime/vfs/vfs.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {
VmValue InvokeDirect(Interpreter& vm, const char* owner, const char* name, const char* signature,
                     std::vector<VmValue> arguments);

[[nodiscard]] const LinkedField& ObjectField(Interpreter& vm, const VmObjectRef object, const std::string& name,
                                             const std::string& descriptor) {
    const auto field = vm.Linker().FindFieldRecursive(vm.Model().ObjectClass(object), name, descriptor);
    if (!field) throw DexVmError{DexVmErrorReason::internal_invariant, "missing BootDex field: " + name};
    return vm.Linker().Field(*field);
}

void SetIntField(Interpreter& vm, const VmObjectRef object, const std::string& name, const std::int32_t value) {
    const auto& field = ObjectField(vm, object, name, "I");
    vm.Model().InstanceSlots(object)[field.slot] = {static_cast<std::uint32_t>(value), SlotTag::cat1};
}

[[nodiscard]] std::int32_t GetCat1Field(Interpreter& vm, const VmObjectRef object, const std::string& name,
                                        const std::string& descriptor) {
    const auto& field = ObjectField(vm, object, name, descriptor);
    return static_cast<std::int32_t>(vm.Model().InstanceSlots(object)[field.slot].bits);
}

void SetShortField(Interpreter& vm, const VmObjectRef object, const std::string& name, const std::int16_t value) {
    const auto& field = ObjectField(vm, object, name, "S");
    vm.Model().InstanceSlots(object)[field.slot] = {static_cast<std::uint32_t>(static_cast<std::int32_t>(value)),
                                                    SlotTag::cat1};
}

[[nodiscard]] std::int64_t GetLongField(Interpreter& vm, const VmObjectRef object, const std::string& name) {
    const auto& field = ObjectField(vm, object, name, "J");
    const auto slots = vm.Model().InstanceSlots(object);
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(slots[field.slot].bits) |
                                     (static_cast<std::uint64_t>(slots[field.slot + 1U].bits) << 32U));
}

[[noreturn]] void ThrowErrno(Interpreter& vm, const std::string_view function, const std::int32_t error_number,
                             const std::string_view detail = {}) {
    const auto function_ref = vm.NewStringUtf8(function);
    const auto roots = vm.ProtectReferences(std::array{function_ref});
    const auto exception = vm.NewIntrinsicInstance("Llibcore/io/ErrnoException;");
    const auto exception_root = vm.ProtectReferences(std::array{exception});
    InvokeDirect(vm, "Llibcore/io/ErrnoException;", "<init>", "(Ljava/lang/String;I)V",
                 {VmValue::Ref(exception), VmValue::Ref(function_ref), VmValue::Int(error_number)});
    throw VmJavaThrow{"Llibcore/io/ErrnoException;",
                      detail.empty() ? std::string(function) + " failed" : std::string(detail), exception};
}

std::uint32_t MappingWord(IntrinsicContext& call, std::size_t index, std::string_view function) {
    const auto value = call.arguments[index].AsLong();
    if (value < 0 || value > std::numeric_limits<std::uint32_t>::max()) ThrowErrno(call.vm, function, 22);
    return static_cast<std::uint32_t>(value);
}

std::int64_t CheckMappingResult(Interpreter& vm, std::string_view function, std::int64_t result) {
    constexpr int kEnosys = 38;
    if (result >= 0) return result;
    if (result == -kEnosys && vm.Ledger()) vm.Ledger()->RecordUnimplemented("dexvm.posix." + std::string(function), 0);
    ThrowErrno(vm, function, static_cast<std::int32_t>(-result));
}

std::span<std::byte> PosixBytes(IntrinsicContext& call, VmObjectRef object, int offset, int count, bool write) {
    if (!object.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "buffer == null"};
    if (call.vm.Buffers().IsBuffer(object)) {
        const auto view = call.vm.Buffers().Read(object, false);
        if (!view.direct_address)
            throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "expected direct ByteBuffer"};
        if (offset < 0 || count < 0 || offset > view.capacity - count)
            throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "bytes range exceeds buffer"};
        if (count == 0) return {};
        return {call.vm.GuestMemory().Resolve(view.direct_address->Add(static_cast<std::uint32_t>(offset)),
                                              static_cast<std::uint32_t>(count), write),
                static_cast<std::size_t>(count)};
    }
    const auto bytes = call.vm.Model().PrimitiveArrayBytes(object);
    if (offset < 0 || count < 0 ||
        static_cast<std::uint64_t>(offset) + static_cast<std::uint64_t>(count) > bytes.size())
        throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;", "bytes range exceeds array"};
    return bytes.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(count));
}

std::int32_t FileDescriptorHandle(IntrinsicContext& call, const VmObjectRef descriptor) {
    if (!descriptor.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "fd == null"};
    return GetCat1Field(call.vm, descriptor, "descriptor", "I");
}

[[nodiscard]] IoRuntime::DescriptorState& PosixDescriptor(IntrinsicContext& call, const VmObjectRef descriptor,
                                                          const std::string_view function) {
    const auto handle = FileDescriptorHandle(call, descriptor);
    if (const auto* state = call.vm.IO().FindDescriptor(descriptor);
        state && state->kind == IoRuntime::DescriptorKind::interface_query) {
        if (auto* ledger = call.vm.Ledger())
            ledger->RecordUnimplemented("dexvm.posix.interface_query." + std::string(function), 0);
        ThrowErrno(call.vm, function, 38);
    }
    try {
        return call.vm.IO().ResolveDescriptor(descriptor, handle);
    } catch (const IoRuntimeError& error) {
        ThrowErrno(call.vm, function, error.ErrorNumber(), error.what());
    }
}

// Blocking pipe IO must not hold the VM execution lock: wait for readiness
// with the lock released, then do the (now non-blocking) IO under it.
void WaitForPipe(IntrinsicContext& call, const VmObjectRef descriptor, const bool write) {
    const auto pipe = call.vm.IO().PipeHandle(descriptor);
    auto* file_system = call.vm.IO().FileSystem();
    if (!pipe || file_system == nullptr) return;
    const auto depth = call.vm.ExecutionLock().ReleaseForBlocking();
    try {
        file_system->WaitStreamReady(*pipe, write);
    } catch (...) {
        call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
        throw;
    }
    call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
}

[[nodiscard]] VmObjectRef NewStructStat(IntrinsicContext& call, const IoFileInfo& info) {
    const auto result = call.vm.NewIntrinsicInstance("Llibcore/io/StructStat;");
    const auto root = call.vm.ProtectReferences(std::array{result});
    constexpr std::int32_t kRegular = 0100000 | 0644;
    constexpr std::int32_t kDirectory = 0040000 | 0755;
    InvokeDirect(call.vm, "Llibcore/io/StructStat;", "<init>", "(JJIJIIJJJJJJJ)V",
                 {VmValue::Ref(result), VmValue::Long(info.device), VmValue::Long(info.inode),
                  VmValue::Int(info.mode           ? static_cast<std::int32_t>(info.mode)
                               : info.is_directory ? kDirectory
                                                   : kRegular),
                  VmValue::Long(1), VmValue::Int(static_cast<std::int32_t>(info.uid)),
                  VmValue::Int(static_cast<std::int32_t>(info.gid)), VmValue::Long(0),
                  VmValue::Long(static_cast<std::int64_t>(info.size)), VmValue::Long(0),
                  VmValue::Long(info.modified_millis / 1000), VmValue::Long(0), VmValue::Long(4096),
                  VmValue::Long(static_cast<std::int64_t>((info.size + 511U) / 512U))});
    return result;
}

IntrinsicHandler NoopVoid() {
    return [](IntrinsicContext&) { return VmValue::Void(); };
}

struct ParsedUri final {
    std::string spec;
    std::optional<std::string> scheme;
    std::string scheme_specific_part;
    std::optional<std::string> authority;
    std::optional<std::string> path;
    std::optional<std::string> query;
    std::optional<std::string> fragment;
    bool absolute{};
    bool opaque{};
};

[[noreturn]] void UriSyntax(const std::string_view spec, const std::string_view reason) {
    throw VmJavaThrow{"Ljava/net/URISyntaxException;", std::string(reason) + ": " + std::string(spec)};
}

void ValidateUriComponent(const std::string_view spec, const std::string_view component) {
    const auto hex = [](const unsigned char value) { return std::isxdigit(value) != 0; };
    for (std::size_t index = 0; index < component.size(); ++index) {
        const auto value = static_cast<unsigned char>(component[index]);
        if (value <= 0x20U || value == 0x7fU) UriSyntax(spec, "Illegal character in URI");
        if (value == '%') {
            if (index + 2U >= component.size() || !hex(static_cast<unsigned char>(component[index + 1U])) ||
                !hex(static_cast<unsigned char>(component[index + 2U]))) {
                UriSyntax(spec, "Invalid percent escape");
            }
            index += 2U;
        }
    }
}

ParsedUri ParseUri(std::string spec) {
    ParsedUri result;
    result.spec = std::move(spec);
    const auto view = std::string_view(result.spec);
    const auto fragment_start = view.find('#');
    const auto main_end = fragment_start == std::string_view::npos ? view.size() : fragment_start;
    if (fragment_start != std::string_view::npos) {
        result.fragment = std::string(view.substr(fragment_start + 1U));
        ValidateUriComponent(view, *result.fragment);
    }

    std::size_t start{};
    const auto colon = view.substr(0, main_end).find(':');
    const auto first_delimiter = view.substr(0, main_end).find_first_of("/?");
    if (colon != std::string_view::npos && (first_delimiter == std::string_view::npos || colon < first_delimiter)) {
        if (colon == 0U || std::isalpha(static_cast<unsigned char>(view[0])) == 0) {
            UriSyntax(view, "Invalid URI scheme");
        }
        for (std::size_t index = 1; index < colon; ++index) {
            const auto value = static_cast<unsigned char>(view[index]);
            if (std::isalnum(value) == 0 && value != '+' && value != '-' && value != '.') {
                UriSyntax(view, "Invalid URI scheme");
            }
        }
        result.absolute = true;
        result.scheme = std::string(view.substr(0, colon));
        start = colon + 1U;
        if (start == main_end) UriSyntax(view, "Scheme-specific part expected");
    }

    result.scheme_specific_part = std::string(view.substr(start, main_end - start));
    ValidateUriComponent(view, result.scheme_specific_part);
    result.opaque =
        result.absolute && (result.scheme_specific_part.empty() || result.scheme_specific_part.front() != '/');
    if (result.opaque) return result;

    std::size_t path_start = start;
    if (start + 1U < main_end && view.substr(start, 2U) == "//") {
        const auto authority_start = start + 2U;
        auto authority_end = view.find_first_of("/?", authority_start);
        if (authority_end == std::string_view::npos || authority_end > main_end) authority_end = main_end;
        if (authority_start == main_end) UriSyntax(view, "Authority expected");
        if (authority_start < authority_end) {
            result.authority = std::string(view.substr(authority_start, authority_end - authority_start));
            ValidateUriComponent(view, *result.authority);
        }
        path_start = authority_end;
    }
    auto query_start = view.find('?', path_start);
    if (query_start == std::string_view::npos || query_start > main_end) query_start = main_end;
    result.path = std::string(view.substr(path_start, query_start - path_start));
    ValidateUriComponent(view, *result.path);
    if (query_start < main_end) {
        result.query = std::string(view.substr(query_start + 1U, main_end - query_start - 1U));
        ValidateUriComponent(view, *result.query);
    }
    return result;
}

IntrinsicClassDecl Declare_java_net_HttpURLConnection_Shell() {
    return std::move(IntrinsicClassBuilder::Class("Ljava/net/HttpURLConnection;", "Ljava/net/URLConnection;")
                         .MergesBootClass())
        .Build();
}

[[noreturn]] void MalformedUrl(const std::string_view spec, const std::string_view reason) {
    throw VmJavaThrow{"Ljava/net/MalformedURLException;", std::string(reason) + ": " + std::string(spec)};
}

IntrinsicClassDecl Declare_java_net_URL_Class() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/net/URL;", "Ljava/lang/Object;", {"Ljava/io/Serializable;"},
                                                kAccPublic | kAccFinal);
    const auto protocol = builder.BoundInstanceField("protocol", "Ljava/lang/String;", kAccPrivate);
    const auto authority = builder.BoundInstanceField("authority", "Ljava/lang/String;", kAccPrivate);
    const auto host = builder.BoundInstanceField("host", "Ljava/lang/String;", kAccPrivate);
    const auto port = builder.BoundInstanceField("port", "I", kAccPrivate);
    const auto file = builder.BoundInstanceField("file", "Ljava/lang/String;", kAccPrivate);
    const auto ref = builder.BoundInstanceField("ref", "Ljava/lang/String;", kAccPrivate);
    const auto user_info = builder.BoundInstanceField("userInfo", "Ljava/lang/String;", kAccPrivate | kAccTransient);
    const auto path = builder.BoundInstanceField("path", "Ljava/lang/String;", kAccPrivate | kAccTransient);
    const auto query = builder.BoundInstanceField("query", "Ljava/lang/String;", kAccPrivate | kAccTransient);
    // Temporary URL replacement compatibility: API19 URL.java component
    // constructors/fixURL(false). Remove with the java.net ROM migration.
    constexpr auto components_signature =
        "(Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;Ljava/net/URLStreamHandler;)V";
    builder.Constructor(components_signature, [=](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto port_value = call.Int(2);
        if (port_value < -1) MalformedUrl({}, "port < -1");
        const auto protocol_value = call.Vm().StringUtf8(call.NonNullRef(0, "protocol"));
        const auto host_ref = call.Ref(1);
        std::optional<std::string> host_value;
        if (host_ref.IsValid()) {
            host_value = call.Vm().StringUtf8(host_ref);
            if (host_value->find(':') != std::string::npos && !host_value->starts_with('['))
                *host_value = "[" + *host_value + "]";
        }
        auto file_value = call.Vm().StringUtf8(call.NonNullRef(3, "file"));
        // UrlUtils.authoritySafePath: an authority requires a separating slash.
        if (host_value && !host_value->empty() && !file_value.empty() && !file_value.starts_with('/'))
            file_value.insert(file_value.begin(), '/');
        const auto hash = file_value.find('#');
        const auto file_part = file_value.substr(0, hash);
        const auto question = file_part.find('?');
        const auto optional_string = [&](const std::optional<std::string>& value) {
            return value ? call.Vm().NewStringUtf8(*value) : VmObjectRef{};
        };
        call.SetRef(protocol, call.Ref(0));
        call.SetRef(host, optional_string(host_value));
        call.SetInt(port, port_value);
        std::optional<std::string> authority_value;
        if (host_value && !host_value->empty()) {
            authority_value = *host_value;
            if (port_value != -1) *authority_value += ":" + std::to_string(port_value);
        }
        call.SetRef(authority, optional_string(authority_value));
        call.SetRef(file, call.Vm().NewStringUtf8(file_part));
        call.SetRef(ref,
                    hash == std::string::npos ? VmObjectRef{} : call.Vm().NewStringUtf8(file_value.substr(hash + 1)));
        call.SetRef(path, call.Vm().NewStringUtf8(file_part.substr(0, question)));
        call.SetRef(query, question == std::string::npos ? VmObjectRef{}
                                                         : call.Vm().NewStringUtf8(file_part.substr(question + 1)));
        call.SetRef(user_info, VmObjectRef{});  // ROM fixURL(false) does not split host user-info.
        if (call.Ref(4).IsValid()) {
            if (auto* ledger = call.Vm().Ledger()) ledger->RecordUnimplemented("dexvm.url.custom_handler", 0);
            MalformedUrl({}, "custom URLStreamHandler is unsupported");
        }
        if (protocol_value != "http" && protocol_value != "https" && protocol_value != "file" &&
            protocol_value != "jar")
            MalformedUrl({}, "Unknown protocol");
        return VmValue::Void();
    });
    builder.Constructor("(Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;)V", [=](IntrinsicContext& c) {
        // URL.java delegates to the five-argument constructor with handler=null.
        InvokeDirect(c.vm, "Ljava/net/URL;", "<init>", components_signature,
                     {VmValue::Ref(c.receiver), c.arguments[0], c.arguments[1], c.arguments[2], c.arguments[3],
                      VmValue::Ref(VmObjectRef{})});
        return VmValue::Void();
    });
    builder.Constructor("(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V", [=](IntrinsicContext& c) {
        // URL.java delegates with the protocol default port (-1) and handler=null.
        InvokeDirect(c.vm, "Ljava/net/URL;", "<init>", components_signature,
                     {VmValue::Ref(c.receiver), c.arguments[0], c.arguments[1], VmValue::Int(-1), c.arguments[2],
                      VmValue::Ref(VmObjectRef{})});
        return VmValue::Void();
    });
    builder.Constructor("(Ljava/lang/String;)V", [=](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto input = call.Ref(0);
        if (!input.IsValid()) MalformedUrl({}, "URL spec is null");
        const auto input_text = call.Vm().StringUtf8(input);
        const std::string trimmed(core::TrimAsciiWhitespace(input_text));
        ParsedUri parsed;
        auto prefix = trimmed.substr(0, std::min<std::size_t>(trimmed.size(), 4));
        std::transform(prefix.begin(), prefix.end(), prefix.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (prefix == "jar:") {
            // API19 JarHandler.parseURL treats the whole suffix as file, including
            // raw %, spaces, '?' and '#'; it does not use URI component validation.
            parsed.absolute = true;
            parsed.scheme = "jar";
            parsed.scheme_specific_part = trimmed.substr(4);
        } else {
            try {
                parsed = ParseUri(std::string(trimmed));
            } catch (const VmJavaThrow&) {
                MalformedUrl(trimmed, "Invalid URL");
            }
        }
        if (!parsed.absolute || !parsed.scheme.has_value()) {
            MalformedUrl(trimmed, "Protocol not found");
        }
        if (!parsed.authority.has_value() && parsed.scheme_specific_part.starts_with("//")) {
            parsed.authority = std::string{};
        }
        auto scheme = *parsed.scheme;
        std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                       [](const unsigned char value) { return static_cast<char>(std::tolower(value)); });
        if (scheme != "http" && scheme != "https" && scheme != "file" && scheme != "jar") {
            MalformedUrl(trimmed, "Unknown protocol");
        }

        std::optional<std::string> parsed_user_info;
        std::string parsed_host;
        std::int32_t parsed_port = -1;
        if (parsed.authority.has_value()) {
            auto host_and_port = std::string_view(*parsed.authority);
            const auto user_end = host_and_port.rfind('@');
            if (user_end != std::string_view::npos) {
                parsed_user_info = std::string(host_and_port.substr(0, user_end));
                host_and_port.remove_prefix(user_end + 1U);
            }

            std::string_view port_view;
            if (!host_and_port.empty() && host_and_port.front() == '[') {
                const auto close = host_and_port.find(']');
                if (close == std::string_view::npos) {
                    MalformedUrl(trimmed, "Invalid IPv6 host");
                }
                parsed_host = std::string(host_and_port.substr(0, close + 1U));
                if (close + 1U < host_and_port.size()) {
                    if (host_and_port[close + 1U] != ':') {
                        MalformedUrl(trimmed, "Invalid authority");
                    }
                    port_view = host_and_port.substr(close + 2U);
                }
            } else {
                const auto separator = host_and_port.rfind(':');
                if (separator != std::string_view::npos) {
                    parsed_host = std::string(host_and_port.substr(0, separator));
                    port_view = host_and_port.substr(separator + 1U);
                } else {
                    parsed_host = std::string(host_and_port);
                }
            }

            if (!port_view.empty()) {
                std::uint32_t value{};
                const auto result = std::from_chars(port_view.data(), port_view.data() + port_view.size(), value);
                if (result.ec != std::errc{} || result.ptr != port_view.data() + port_view.size() ||
                    value > UINT16_MAX) {
                    MalformedUrl(trimmed, "Invalid port");
                }
                parsed_port = static_cast<std::int32_t>(value);
            } else if (!host_and_port.empty() && host_and_port.back() == ':') {
                MalformedUrl(trimmed, "Invalid port");
            }
        }
        if ((scheme == "http" || scheme == "https") && (!parsed.authority.has_value() || parsed_host.empty())) {
            MalformedUrl(trimmed, "Host expected");
        }

        const auto optional_ref = [&](const std::optional<std::string>& value) {
            return value.has_value() ? call.Vm().NewStringUtf8(*value) : VmObjectRef{};
        };
        if (scheme == "jar" && (parsed.scheme_specific_part.find("!/") == std::string::npos ||
                                parsed.scheme_specific_part.find(':') == std::string::npos)) {
            MalformedUrl(trimmed, "Invalid jar URL");
        }
        const auto path_text = scheme == "jar" ? parsed.scheme_specific_part : parsed.path.value_or("");
        std::string file_text = path_text;
        if (parsed.query.has_value()) {
            file_text.push_back('?');
            file_text += *parsed.query;
        }

        call.SetRef(protocol, call.Vm().NewStringUtf8(scheme));
        call.SetRef(authority, optional_ref(parsed.authority));
        call.SetRef(host, call.Vm().NewStringUtf8(parsed_host));
        call.SetInt(port, parsed_port);
        call.SetRef(file, call.Vm().NewStringUtf8(file_text));
        call.SetRef(ref, optional_ref(parsed.fragment));
        call.SetRef(user_info, optional_ref(parsed_user_info));
        call.SetRef(path, call.Vm().NewStringUtf8(path_text));
        call.SetRef(query, optional_ref(parsed.query));
        return VmValue::Void();
    });

    const auto raw = [](const IntrinsicFieldHandle field) {
        return [field](IntrinsicContext& context) { return VmValue::Ref(IntrinsicCall(context).GetRef(field)); };
    };
    builder.FinalMethod("getProtocol", "()Ljava/lang/String;", raw(protocol));
    builder.FinalMethod("getAuthority", "()Ljava/lang/String;", raw(authority));
    builder.FinalMethod("getUserInfo", "()Ljava/lang/String;", raw(user_info));
    builder.FinalMethod("getHost", "()Ljava/lang/String;", raw(host));
    builder.FinalMethod("getPort", "()I", [port](IntrinsicContext& context) {
        return VmValue::Int(IntrinsicCall(context).GetInt(port));
    });
    builder.FinalMethod("getDefaultPort", "()I", [protocol](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto value = call.Vm().StringUtf8(call.GetRef(protocol));
        if (value == "http") return VmValue::Int(80);
        if (value == "https") return VmValue::Int(443);
        return VmValue::Int(-1);
    });
    builder.FinalMethod("getFile", "()Ljava/lang/String;", raw(file));
    builder.FinalMethod("getPath", "()Ljava/lang/String;", raw(path));
    builder.FinalMethod("getQuery", "()Ljava/lang/String;", raw(query));
    builder.FinalMethod("getRef", "()Ljava/lang/String;", raw(ref));

    const auto external_form = [=](IntrinsicContext& context) {
        IntrinsicCall call(context);
        std::string rendered = call.Vm().StringUtf8(call.GetRef(protocol));
        rendered.push_back(':');
        const auto authority_ref = call.GetRef(authority);
        if (authority_ref.IsValid()) {
            rendered += "//";
            rendered += call.Vm().StringUtf8(authority_ref);
        }
        rendered += call.Vm().StringUtf8(call.GetRef(path));
        const auto query_ref = call.GetRef(query);
        if (query_ref.IsValid()) {
            rendered.push_back('?');
            rendered += call.Vm().StringUtf8(query_ref);
        }
        const auto fragment_ref = call.GetRef(ref);
        if (fragment_ref.IsValid()) {
            rendered.push_back('#');
            rendered += call.Vm().StringUtf8(fragment_ref);
        }
        return VmValue::Ref(call.Vm().NewStringUtf8(rendered));
    };
    builder.FinalMethod("toExternalForm", "()Ljava/lang/String;", external_form);
    builder.FinalOverrideMethod("toString", "()Ljava/lang/String;", external_form);

    // URLStreamHandler.toExternalForm(url, true): escape only characters URI
    // forbids (spaces, quotes, non-ASCII, ...); existing %XX stay as they are.
    const auto lenient_form = [external_form](IntrinsicContext& context) {
        const auto text = context.vm.StringUtf8(external_form(context).ref);
        constexpr std::string_view kLegal =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~:/?#[]@!$&'()*+,;=%";
        std::string escaped;
        for (const auto value : text) {
            const auto byte = static_cast<unsigned char>(value);
            if (byte < 0x80U && kLegal.find(static_cast<char>(byte)) != std::string_view::npos) {
                escaped.push_back(static_cast<char>(byte));
                continue;
            }
            constexpr char kHex[] = "0123456789ABCDEF";
            escaped.push_back('%');
            escaped.push_back(kHex[byte >> 4U]);
            escaped.push_back(kHex[byte & 0xfU]);
        }
        return escaped;
    };
    const auto new_uri = [](IntrinsicContext& context, const std::string& text) {
        const auto uri = context.vm.NewIntrinsicInstance("Ljava/net/URI;");
        const auto roots = context.vm.ProtectReferences(std::array{uri});
        InvokeDirect(context.vm, "Ljava/net/URI;", "<init>", "(Ljava/lang/String;)V",
                     {VmValue::Ref(uri), VmValue::Ref(context.vm.NewStringUtf8(text))});
        return VmValue::Ref(uri);
    };
    builder.FinalMethod("toURI", "()Ljava/net/URI;", [external_form, new_uri](IntrinsicContext& context) {
        return new_uri(context, context.vm.StringUtf8(external_form(context).ref));
    });
    builder.FinalMethod("toURILenient", "()Ljava/net/URI;", [lenient_form, new_uri](IntrinsicContext& context) {
        return new_uri(context, lenient_form(context));
    });
    // URL(URL context, String spec[, handler]): absolute specs parse alone;
    // relative ones resolve against the context as java.net.URI does.
    const auto relative = [lenient_form](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto base = call.Ref(0);
        const auto spec_ref = call.Ref(1);
        if (!spec_ref.IsValid()) MalformedUrl({}, "spec == null");
        if (context.arguments.size() > 2 && call.Ref(2).IsValid()) {
            if (auto* ledger = context.vm.Ledger()) ledger->RecordUnimplemented("dexvm.url.custom_handler", 0);
            MalformedUrl({}, "custom URLStreamHandler is unsupported");
        }
        std::string spec(core::TrimAsciiWhitespace(context.vm.StringUtf8(spec_ref)));
        const auto colon = spec.find(':');
        const auto delimiter = spec.find_first_of("/?#");
        const bool absolute =
            colon != std::string::npos && colon > 0 && (delimiter == std::string::npos || colon < delimiter);
        std::string resolved = spec;
        if (!absolute) {
            if (!base.IsValid()) MalformedUrl(spec, "Protocol not found");
            IntrinsicContext base_context{context.vm, base, {}};
            const auto base_uri = context.vm.NewIntrinsicInstance("Ljava/net/URI;");
            const auto roots = context.vm.ProtectReferences(std::array{base_uri});
            InvokeDirect(context.vm, "Ljava/net/URI;", "<init>", "(Ljava/lang/String;)V",
                         {VmValue::Ref(base_uri), VmValue::Ref(context.vm.NewStringUtf8(lenient_form(base_context)))});
            std::string escaped;
            for (const auto value : spec) {
                const auto byte = static_cast<unsigned char>(value);
                if (byte <= 0x20U || byte >= 0x7fU || value == '"' || value == '<' || value == '>' || value == '\\' ||
                    value == '^' || value == '`' || value == '{' || value == '|' || value == '}') {
                    constexpr char kHex[] = "0123456789ABCDEF";
                    escaped.push_back('%');
                    escaped.push_back(kHex[byte >> 4U]);
                    escaped.push_back(kHex[byte & 0xfU]);
                } else {
                    escaped.push_back(value);
                }
            }
            const auto target =
                detail::InvokeGuest(context.vm, base_uri, "resolve", "(Ljava/lang/String;)Ljava/net/URI;",
                                    {VmValue::Ref(context.vm.NewStringUtf8(escaped))})
                    .ref;
            resolved =
                context.vm.StringUtf8(detail::InvokeGuest(context.vm, target, "toString", "()Ljava/lang/String;").ref);
        }
        InvokeDirect(context.vm, "Ljava/net/URL;", "<init>", "(Ljava/lang/String;)V",
                     {VmValue::Ref(context.receiver), VmValue::Ref(context.vm.NewStringUtf8(resolved))});
        return VmValue::Void();
    };
    builder.Constructor("(Ljava/net/URL;Ljava/lang/String;)V", relative);
    builder.Constructor("(Ljava/net/URL;Ljava/lang/String;Ljava/net/URLStreamHandler;)V", relative);

    const auto open_connection = [protocol](IntrinsicContext& context) -> VmValue {
        IntrinsicCall call(context);
        const auto protocol_text = call.Vm().StringUtf8(call.GetRef(protocol));
        if (protocol_text == "jar") {
            const auto connection = call.Vm().NewIntrinsicInstance("Lorg/ogplay/io/ClasspathURLConnection;");
            const auto roots = call.Vm().ProtectReferences(std::array{connection, call.Receiver()});
            InvokeDirect(call.Vm(), "Lorg/ogplay/io/ClasspathURLConnection;", "<init>", "(Ljava/net/URL;)V",
                         {VmValue::Ref(connection), VmValue::Ref(call.Receiver())});
            return VmValue::Ref(connection);
        }
        if (protocol_text == "file") {
            throw VmJavaThrow{"Ljava/io/IOException;", "file URL I/O is not implemented"};
        }
        // API 19 URL.setupStreamHandler loads these okhttp.jar handlers by
        // name; openConnection only builds the object, I/O starts at connect.
        const char* handler_descriptor = protocol_text == "https"  ? "Lcom/android/okhttp/HttpsHandler;"
                                         : protocol_text == "http" ? "Lcom/android/okhttp/HttpHandler;"
                                                                   : nullptr;
        if (handler_descriptor == nullptr) {
            throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "URLConnection protocol is not implemented"};
        }
        const auto handler = call.Vm().NewIntrinsicInstance(handler_descriptor);
        const auto roots = call.Vm().ProtectReferences(std::array{handler, call.Receiver()});
        InvokeDirect(call.Vm(), handler_descriptor, "<init>", "()V", {VmValue::Ref(handler)});
        return detail::InvokeGuest(call.Vm(), handler, "openConnection", "(Ljava/net/URL;)Ljava/net/URLConnection;",
                                   {VmValue::Ref(call.Receiver())});
    };
    builder.FinalMethod("openConnection", "()Ljava/net/URLConnection;", open_connection);
    builder.FinalMethod("openStream", "()Ljava/io/InputStream;", [](IntrinsicContext& context) {
        const auto connection =
            detail::InvokeGuest(context.vm, context.receiver, "openConnection", "()Ljava/net/URLConnection;").ref;
        const auto roots = context.vm.ProtectReferences(std::array{connection});
        return detail::InvokeGuest(context.vm, connection, "getInputStream", "()Ljava/io/InputStream;");
    });
    return std::move(builder).Build();
}

struct UrlConnectionDeclaration final {
    IntrinsicClassDecl declaration;
    IntrinsicFieldHandle url;
    IntrinsicFieldHandle connected;
};

UrlConnectionDeclaration Declare_java_net_URLConnection_Fields() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/net/URLConnection;", "Ljava/lang/Object;");
    builder.MergesBootClass();
    const auto url = builder.BoundInstanceField("url", "Ljava/net/URL;", kAccProtected);
    const auto connected = builder.BoundInstanceField("connected", "Z", kAccProtected);
    return {std::move(builder).Build(), url, connected};
}

CoreIntrinsicServices::ClasspathResource JarResource(IntrinsicContext& context, const std::string_view file) {
    const auto unsupported = [&context]() -> void {
        if (auto* ledger = context.vm.Ledger()) {
            ledger->RecordUnimplemented("dexvm.classloader.resource_url", 0);
        }
        throw VmJavaThrow{"Ljava/io/IOException;", "jar URL requires a registered sealed guest archive"};
    };
    const auto separator = file.find("!/");
    if (!file.starts_with("file:/") || file.starts_with("file://") || separator == std::string_view::npos)
        unsupported();
    const auto decode = [](const std::string_view text) {
        const auto encoded = boost::urls::make_pct_string_view(text);
        if (!encoded) throw VmJavaThrow{"Ljava/io/IOException;", "invalid jar URL escape"};
        boost::urls::encoding_opts options;
        options.space_as_plus = false;
        auto result = encoded->decode(options);
        if (result.find('\0') != std::string::npos) {
            throw VmJavaThrow{"Ljava/io/IOException;", "NUL in jar resource name"};
        }
        return result;
    };
    // JarURLConnection.java keeps entryName raw; JarURLConnectionImpl.java
    // applies UriCodec.decode only to the archive's file path.
    return {decode(file.substr(5, separator - 5)), std::string(file.substr(separator + 2))};
}

IntrinsicClassDecl Declare_org_ogplay_io_ClasspathURLConnection_Class(const CoreIntrinsicServices& services,
                                                                      const IntrinsicFieldHandle url,
                                                                      const IntrinsicFieldHandle connected) {
    auto builder = IntrinsicClassBuilder::Class("Lorg/ogplay/io/ClasspathURLConnection;", "Ljava/net/URLConnection;",
                                                {}, kAccPublic | kAccFinal);
    const auto input = builder.BoundInstanceField("input", "Ljava/io/InputStream;", kAccPrivate);
    builder.Constructor("(Ljava/net/URL;)V", [](IntrinsicContext& context) {
        const auto target = IntrinsicCall(context).NonNullRef(0, "url");
        InvokeDirect(context.vm, "Ljava/net/URLConnection;", "<init>", "(Ljava/net/URL;)V",
                     {VmValue::Ref(context.receiver), VmValue::Ref(target)});
        return VmValue::Void();
    });
    const auto connect = [=](IntrinsicContext& context) {
        IntrinsicCall call(context);
        if (call.GetInt(connected)) return VmValue::Void();
        const auto file = detail::InvokeGuest(context.vm, call.GetRef(url), "getFile", "()Ljava/lang/String;").ref;
        const auto roots = context.vm.ProtectReferences(std::array{file});
        auto resource = JarResource(context, context.vm.StringUtf8(file));
        // JarURLConnection's constructor appends a separately stored URL ref.
        const auto fragment = detail::InvokeGuest(context.vm, call.GetRef(url), "getRef", "()Ljava/lang/String;").ref;
        if (fragment.IsValid()) resource.entry_name += "#" + context.vm.StringUtf8(fragment);
        const auto stream = detail::OpenClasspathResource(context, services, resource);
        call.SetRef(input, stream);
        call.SetInt(connected, 1);
        return VmValue::Void();
    };
    builder.OverrideMethod("connect", "()V", connect);
    builder.OverrideMethod("getInputStream", "()Ljava/io/InputStream;", [=](IntrinsicContext& context) {
        if (!detail::InvokeGuest(context.vm, context.receiver, "getDoInput", "()Z").AsInt()) {
            throw VmJavaThrow{"Ljava/net/ProtocolException;", "doInput is false"};
        }
        connect(context);
        return VmValue::Ref(IntrinsicCall(context).GetRef(input));
    });
    return std::move(builder).Build();
}

constexpr boost::urls::grammar::lut_chars kFormUrlEncodedSafe{
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-*_"};

void RequireUtf8Charset(IntrinsicCall& call, const std::size_t argument) {
    auto charset = call.Vm().StringUtf8(call.NonNullRef(argument, "charsetName"));
    for (auto& byte : charset) {
        byte = static_cast<char>(std::toupper(static_cast<unsigned char>(byte)));
    }
    if (charset != "UTF-8" && charset != "UTF8") {
        throw VmJavaThrow{"Ljava/io/UnsupportedEncodingException;", charset};
    }
}

[[nodiscard]] std::u16string Utf8ToUtf16Replacing(const std::string_view input) {
    std::u16string result;
    result.reserve(input.size());
    for (std::size_t index = 0; index < input.size();) {
        const auto first = static_cast<std::uint8_t>(input[index]);
        std::uint32_t code_point{};
        std::size_t count{};
        if (first <= 0x7fU) {
            code_point = first;
            count = 1U;
        } else if (first >= 0xc2U && first <= 0xdfU) {
            code_point = first & 0x1fU;
            count = 2U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            code_point = first & 0x0fU;
            count = 3U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            code_point = first & 0x07U;
            count = 4U;
        }
        bool valid = count != 0U && count <= input.size() - index;
        for (std::size_t offset = 1U; valid && offset < count; ++offset) {
            const auto byte = static_cast<std::uint8_t>(input[index + offset]);
            if ((byte & 0xc0U) != 0x80U) {
                valid = false;
            } else {
                code_point = (code_point << 6U) | (byte & 0x3fU);
            }
        }
        const auto minimum = count == 2U ? 0x80U : count == 3U ? 0x800U : count == 4U ? 0x10000U : 0U;
        valid = valid && code_point >= minimum && code_point <= 0x10ffffU &&
                !(code_point >= 0xd800U && code_point <= 0xdfffU);
        if (!valid) {
            result.push_back(u'\ufffd');
            ++index;
            continue;
        }
        if (code_point <= 0xffffU) {
            result.push_back(static_cast<char16_t>(code_point));
        } else {
            code_point -= 0x10000U;
            result.push_back(static_cast<char16_t>(0xd800U + (code_point >> 10U)));
            result.push_back(static_cast<char16_t>(0xdc00U + (code_point & 0x3ffU)));
        }
        index += count;
    }
    return result;
}

[[nodiscard]] VmObjectRef FormEncode(IntrinsicCall& call, const VmObjectRef input) {
    const auto units = call.Vm().Model().StringChars(input);
    const auto utf8 = core::Utf16ToUtf8(std::span{units}, core::InvalidUtf16Policy::replace, '?');
    if (!utf8.has_value()) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid UTF-16 input"};
    }
    boost::urls::encoding_opts options;
    options.space_as_plus = true;
    return call.Vm().NewStringUtf8(boost::urls::encode(*utf8, kFormUrlEncodedSafe, options));
}

[[nodiscard]] VmObjectRef FormDecode(IntrinsicCall& call, const VmObjectRef input) {
    const auto units = call.Vm().Model().StringChars(input);
    if (std::find(units.begin(), units.end(), u'%') == units.end() &&
        std::find(units.begin(), units.end(), u'+') == units.end()) {
        return input;
    }
    const auto encoded = core::Utf16ToUtf8(std::span{units}, core::InvalidUtf16Policy::replace, '?');
    if (!encoded.has_value()) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid UTF-16 input"};
    }
    const auto parsed = boost::urls::make_pct_string_view(*encoded);
    if (!parsed.has_value()) {
        throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "invalid percent escape"};
    }
    boost::urls::encoding_opts options;
    options.space_as_plus = true;
    return call.Vm().Model().NewString(Utf8ToUtf16Replacing(parsed->decode(options)));
}

IntrinsicHandler FormCodec(const bool encode, const bool named_charset) {
    return [encode, named_charset](IntrinsicContext& context) {
        IntrinsicCall call(context);
        const auto input = call.NonNullRef(0, "input");
        if (named_charset) RequireUtf8Charset(call, 1U);
        return VmValue::Ref(encode ? FormEncode(call, input) : FormDecode(call, input));
    };
}

IntrinsicClassDecl Declare_java_net_URLEncoder_Class() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/net/URLEncoder;", "Ljava/lang/Object;");
    builder.Constructor("()V", NoopVoid(), kAccPrivate);
    builder.StaticMethod("encode", "(Ljava/lang/String;)Ljava/lang/String;", FormCodec(true, false));
    builder.StaticMethod("encode", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", FormCodec(true, true));
    return std::move(builder).Build();
}

IntrinsicClassDecl Declare_java_net_URLDecoder_Class() {
    auto builder = IntrinsicClassBuilder::Class("Ljava/net/URLDecoder;", "Ljava/lang/Object;");
    builder.Constructor("()V", NoopVoid());
    builder.StaticMethod("decode", "(Ljava/lang/String;)Ljava/lang/String;", FormCodec(false, false));
    builder.StaticMethod("decode", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;", FormCodec(false, true));
    return std::move(builder).Build();
}

VmValue InvokeDirect(Interpreter& vm, const char* owner, const char* name, const char* signature,
                     std::vector<VmValue> arguments) {
    const auto method = vm.Linker().FindDirectMethod(vm.Linker().ResolveDescriptor(owner), name, signature);
    if (!method) throw DexVmError(DexVmErrorReason::unresolved_reference, std::string(owner) + "->" + name + signature);
    const auto outcome = vm.Call(*method, arguments);
    if (outcome.exception.IsValid())
        throw VmJavaThrow{vm.Linker().Class(outcome.exception_class).descriptor, outcome.exception_message,
                          outcome.exception};
    return outcome.value;
}

VmObjectRef NewAddress(Interpreter& vm, std::span<const std::byte> bytes, VmObjectRef host = VmObjectRef{0}) {
    const auto array = vm.Model().NewPrimitiveArray(vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                                                    static_cast<JniSize>(bytes.size()));
    vm.Model().WriteByteRegion(array, 0, bytes);
    const auto roots = vm.ProtectReferences(std::array{array, host});
    return InvokeDirect(vm, "Ljava/net/InetAddress;", "getByAddress", "(Ljava/lang/String;[B)Ljava/net/InetAddress;",
                        {VmValue::Ref(host), VmValue::Ref(array)})
        .ref;
}

std::optional<std::vector<std::byte>> ParseAddress(const std::string_view value) {
    auto ipv4 = [](const std::string_view input) -> std::optional<std::array<std::byte, 4>> {
        std::array<std::byte, 4> bytes{};
        std::size_t offset{};
        for (std::size_t part = 0; part < 4; ++part) {
            const auto end = part == 3 ? input.size() : input.find('.', offset);
            if (end == std::string_view::npos || end == offset) return std::nullopt;
            unsigned value{};
            const auto [last, error] = std::from_chars(input.data() + offset, input.data() + end, value);
            if (error != std::errc{} || last != input.data() + end || value > 255) return std::nullopt;
            bytes[part] = static_cast<std::byte>(value);
            offset = end + 1;
        }
        if (offset != input.size() + 1) return std::nullopt;
        return bytes;
    };
    if (const auto parsed = ipv4(value)) return std::vector<std::byte>(parsed->begin(), parsed->end());

    std::string_view text = value;
    if (text.size() > 2 && text.front() == '[' && text.back() == ']') text = text.substr(1, text.size() - 2);
    if (text.find(':') == std::string_view::npos) return std::nullopt;
    auto parse_words = [&ipv4](std::string_view input, std::vector<std::uint16_t>& output) {
        if (input.empty()) return true;
        std::size_t offset{};
        while (offset < input.size()) {
            const auto end = input.find(':', offset);
            const auto token =
                input.substr(offset, end == std::string_view::npos ? input.size() - offset : end - offset);
            if (token.empty()) return false;
            if (token.find('.') != std::string_view::npos) {
                const auto tail = ipv4(token);
                if (!tail) return false;
                output.push_back(static_cast<std::uint16_t>((std::to_integer<unsigned>((*tail)[0]) << 8) |
                                                            std::to_integer<unsigned>((*tail)[1])));
                output.push_back(static_cast<std::uint16_t>((std::to_integer<unsigned>((*tail)[2]) << 8) |
                                                            std::to_integer<unsigned>((*tail)[3])));
            } else {
                if (token.size() > 4) return false;
                unsigned word{};
                const auto [last, error] = std::from_chars(token.data(), token.data() + token.size(), word, 16);
                if (error != std::errc{} || last != token.data() + token.size()) return false;
                output.push_back(static_cast<std::uint16_t>(word));
            }
            if (end == std::string_view::npos) break;
            offset = end + 1;
        }
        return true;
    };
    const auto compression = text.find("::");
    if (compression != std::string_view::npos && text.find("::", compression + 2) != std::string_view::npos)
        return std::nullopt;
    std::vector<std::uint16_t> left, right;
    if (compression == std::string_view::npos) {
        if (!parse_words(text, left) || left.size() != 8) return std::nullopt;
    } else {
        if (!parse_words(text.substr(0, compression), left) || !parse_words(text.substr(compression + 2), right) ||
            left.size() + right.size() >= 8)
            return std::nullopt;
    }
    std::array<std::uint16_t, 8> words{};
    std::copy(left.begin(), left.end(), words.begin());
    std::copy(right.begin(), right.end(), words.end() - static_cast<std::ptrdiff_t>(right.size()));
    std::vector<std::byte> result(16);
    for (std::size_t i = 0; i < words.size(); ++i) {
        result[i * 2] = static_cast<std::byte>(words[i] >> 8);
        result[i * 2 + 1] = static_cast<std::byte>(words[i]);
    }
    return result;
}

std::string NumericAddress(Interpreter& vm, const VmObjectRef address) {
    const auto bytes_ref = detail::InvokeGuest(vm, address, "getAddress", "()[B").ref;
    const auto bytes = vm.Model().ReadByteRegion(bytes_ref, 0, vm.Model().ArrayLength(bytes_ref));
    if (bytes.size() == 4) {
        return std::to_string(std::to_integer<unsigned>(bytes[0])) + "." +
               std::to_string(std::to_integer<unsigned>(bytes[1])) + "." +
               std::to_string(std::to_integer<unsigned>(bytes[2])) + "." +
               std::to_string(std::to_integer<unsigned>(bytes[3]));
    }
    if (bytes.size() == 16) {
        std::array<std::uint16_t, 8> words{};
        for (std::size_t i = 0; i < words.size(); ++i)
            words[i] = static_cast<std::uint16_t>((std::to_integer<unsigned>(bytes[i * 2]) << 8) |
                                                  std::to_integer<unsigned>(bytes[i * 2 + 1]));
        std::size_t best_start = 8, best_length{};
        for (std::size_t i = 0; i < 8;) {
            if (words[i] != 0) {
                ++i;
                continue;
            }
            const auto start = i;
            while (i < 8 && words[i] == 0)
                ++i;
            if (i - start > best_length && i - start >= 2) {
                best_start = start;
                best_length = i - start;
            }
        }
        std::string result;
        for (std::size_t i = 0; i < 8;) {
            if (i == best_start) {
                result += "::";
                i += best_length;
            } else {
                if (!result.empty() && result.back() != ':') result += ':';
                char buffer[5]{};
                const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), words[i], 16);
                result.append(buffer, converted.ptr);
                ++i;
            }
        }
        return result;
    }
    throw DexVmError(DexVmErrorReason::internal_invariant, "InetAddress has invalid byte length");
}

[[noreturn]] void ThrowGai(Interpreter& vm, const std::string_view function, const std::int32_t error,
                           const std::string_view message) {
    const auto function_ref = vm.NewStringUtf8(function);
    const auto function_root = vm.ProtectReferences(std::array{function_ref});
    const auto exception = vm.NewIntrinsicInstance("Llibcore/io/GaiException;");
    const auto exception_root = vm.ProtectReferences(std::array{exception});
    InvokeDirect(vm, "Llibcore/io/GaiException;", "<init>", "(Ljava/lang/String;I)V",
                 {VmValue::Ref(exception), VmValue::Ref(function_ref), VmValue::Int(error)});
    throw VmJavaThrow{"Llibcore/io/GaiException;", std::string(message), exception};
}

// bionic libc/netbsd/net/getaddrinfo.c ai_errlist.
const char* Api19GaiStrerror(const std::int32_t error) {
    switch (error) {
    case 0: return "Success";
    case 1: return "Address family for hostname not supported";
    case 2: return "Temporary failure in name resolution";
    case 3: return "Invalid value for ai_flags";
    case 4: return "Non-recoverable failure in name resolution";
    case 5: return "ai_family not supported";
    case 6: return "Memory allocation failure";
    case 7: return "No address associated with hostname";
    case 8: return "hostname nor servname provided, or not known";
    case 9: return "servname not supported for ai_socktype";
    case 10: return "ai_socktype not supported";
    case 11: return "System error returned in errno";
    case 12: return "Invalid value for hints";
    case 13: return "Resolved protocol is unknown";
    case 14: return "Argument buffer overflow";
    default: return "Unknown error";
    }
}

// bionic strerror text for the errnos file and socket natives report.
const char* Api19Strerror(const std::int32_t error) {
    switch (error) {
    case 1: return "Operation not permitted";
    case 2: return "No such file or directory";
    case 4: return "Interrupted system call";
    case 5: return "I/O error";
    case 9: return "Bad file number";
    case 11: return "Try again";
    case 12: return "Out of memory";
    case 13: return "Permission denied";
    case 14: return "Bad address";
    case 17: return "File exists";
    case 19: return "No such device";
    case 20: return "Not a directory";
    case 21: return "Is a directory";
    case 22: return "Invalid argument";
    case 24: return "Too many open files";
    case 25: return "Not a typewriter";
    case 28: return "No space left on device";
    case 29: return "Illegal seek";
    case 30: return "Read-only file system";
    case 32: return "Broken pipe";
    case 38: return "Function not implemented";
    case 88: return "Socket operation on non-socket";
    case 89: return "Destination address required";
    case 90: return "Message too long";
    case 91: return "Protocol wrong type for socket";
    case 92: return "Protocol not available";
    case 93: return "Protocol not supported";
    case 94: return "Socket type not supported";
    case 95: return "Operation not supported on transport endpoint";
    case 97: return "Address family not supported by protocol";
    case 98: return "Address already in use";
    case 99: return "Cannot assign requested address";
    case 100: return "Network is down";
    case 101: return "Network is unreachable";
    case 103: return "Software caused connection abort";
    case 104: return "Connection reset by peer";
    case 106: return "Transport endpoint is already connected";
    case 107: return "Transport endpoint is not connected";
    case 110: return "Connection timed out";
    case 111: return "Connection refused";
    case 113: return "No route to host";
    case 114: return "Operation already in progress";
    case 115: return "Operation now in progress";
    default: return "Unknown error";
    }
}

// ---- libcore.io.Posix socket natives over runtime/net ---------------------

constexpr std::int32_t kEbadf = 9;
constexpr std::int32_t kEnodev = 19;
constexpr std::int32_t kEnosysErrno = 38;
constexpr std::int32_t kEaiNoName = 8;
constexpr std::int32_t kEaiNoData = 7;
constexpr std::int32_t kAiNumericHost = 4;
constexpr std::int32_t kAiAddrConfig = 0x400;
// Bounds one Java transfer's host scratch; sockets may return short counts.
constexpr std::size_t kMaxSocketTransfer = 256U * 1024U;

net::GuestNetworkStack& RequireStack(IntrinsicContext& call, const std::string_view function) {
    if (auto* stack = call.vm.Network().Stack()) return *stack;
    if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.posix.network", 0);
    ThrowErrno(call.vm, function, kEnosysErrno, "guest socket stack is not injected");
}

template <class Operation>
auto Unlocked(IntrinsicContext& call, Operation&& operation) {
    const auto depth = call.vm.ExecutionLock().ReleaseForBlocking();
    try {
        auto result = operation();
        call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
        return result;
    } catch (...) {
        call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
        throw;
    }
}

std::int32_t SocketHandle(IntrinsicContext& call, const VmObjectRef descriptor) {
    return FileDescriptorHandle(call, descriptor);
}

// libcore NET_FAILURE_RETRY: an operation on a descriptor closed meanwhile is "Socket closed".
[[noreturn]] void ThrowSocketErrno(IntrinsicContext& call, const VmObjectRef descriptor,
                                   const std::string_view function, const std::int64_t result) {
    const auto error = static_cast<std::int32_t>(-result);
    if (error == kEbadf && descriptor.IsValid() && FileDescriptorHandle(call, descriptor) < 0)
        throw VmJavaThrow{"Ljava/net/SocketException;", "Socket closed"};
    ThrowErrno(call.vm, function, error, std::string(function) + " failed: " + Api19Strerror(error));
}

std::int64_t CheckSocket(IntrinsicContext& call, const VmObjectRef descriptor, const std::string_view function,
                         const std::int64_t result) {
    if (result < 0) ThrowSocketErrno(call, descriptor, function, result);
    return result;
}

[[nodiscard]] VmObjectRef GetRefField(Interpreter& vm, const VmObjectRef object, const std::string& name,
                                      const std::string& descriptor) {
    const auto& field = ObjectField(vm, object, name, descriptor);
    return VmObjectRef{vm.Model().InstanceSlots(object)[field.slot].bits};
}

void SetRefField(Interpreter& vm, const VmObjectRef object, const std::string& name, const std::string& descriptor,
                 const VmObjectRef value) {
    const auto& field = ObjectField(vm, object, name, descriptor);
    vm.Model().InstanceSlots(object)[field.slot] = {value.Value(), SlotTag::ref};
}

std::int32_t SocketDomain(net::GuestNetworkStack& stack, const std::int32_t fd) {
    std::vector<std::byte> value;
    constexpr int kSoDomain = 39;
    if (stack.GetOption(fd, net::kSolSocket, kSoDomain, value) != 0 || value.size() < 4) return net::kAfInet6;
    return static_cast<std::int32_t>(std::to_integer<std::uint32_t>(value[0]) |
                                     (std::to_integer<std::uint32_t>(value[1]) << 8U));
}

// libcore inetAddressToSockaddr: IPv6 sockets see IPv4 addresses IPv4-mapped.
net::GuestSocketAddress SockaddrOf(IntrinsicContext& call, const VmObjectRef address, const std::int32_t port,
                                   const std::int32_t domain) {
    if (!address.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "address == null"};
    if (port < 0 || port > 65535) ThrowErrno(call.vm, "sockaddr", 22);
    const auto bytes_ref = detail::InvokeGuest(call.vm, address, "getAddress", "()[B").ref;
    const auto bytes = call.vm.Model().ReadByteRegion(bytes_ref, 0, call.vm.Model().ArrayLength(bytes_ref));
    net::GuestSocketAddress result;
    result.port = static_cast<std::uint16_t>(port);
    if (bytes.size() == 4) {
        if (domain == net::kAfInet6) {
            result.family = net::kAfInet6;
            result.address[10] = 0xff;
            result.address[11] = 0xff;
            for (std::size_t index = 0; index < 4; ++index)
                result.address[12 + index] = std::to_integer<std::uint8_t>(bytes[index]);
        } else {
            result.family = net::kAfInet;
            for (std::size_t index = 0; index < 4; ++index)
                result.address[index] = std::to_integer<std::uint8_t>(bytes[index]);
        }
        return result;
    }
    if (bytes.size() != 16) throw DexVmError(DexVmErrorReason::internal_invariant, "InetAddress has invalid length");
    result.family = net::kAfInet6;
    for (std::size_t index = 0; index < 16; ++index)
        result.address[index] = std::to_integer<std::uint8_t>(bytes[index]);
    result.scope_id = static_cast<std::uint32_t>(detail::InvokeGuest(call.vm, address, "getScopeId", "()I").AsInt());
    return result;
}

// libcore sockaddrToInetAddress: IPv4-mapped addresses become Inet4Address.
VmObjectRef InetAddressOf(IntrinsicContext& call, const net::GuestSocketAddress& address) {
    if (address.family == net::kAfUnix) {
        std::vector<std::byte> path;
        if (address.abstract_name) path.push_back(std::byte{0});
        for (const auto value : address.path)
            path.push_back(static_cast<std::byte>(value));
        const auto array = call.vm.Model().NewPrimitiveArray(call.vm.Linker().ResolveDescriptor("[B"),
                                                             JniPrimitiveKind::byte, static_cast<JniSize>(path.size()));
        call.vm.Model().WriteByteRegion(array, 0, path);
        const auto root = call.vm.ProtectReferences(std::array{array});
        const auto object = call.vm.NewIntrinsicInstance("Ljava/net/InetUnixAddress;");
        const auto object_root = call.vm.ProtectReferences(std::array{object});
        InvokeDirect(call.vm, "Ljava/net/InetUnixAddress;", "<init>", "([B)V",
                     {VmValue::Ref(object), VmValue::Ref(array)});
        return object;
    }
    const bool mapped = address.family == net::kAfInet6 &&
                        std::all_of(address.address.begin(), address.address.begin() + 10,
                                    [](const std::uint8_t value) { return value == 0; }) &&
                        address.address[10] == 0xff && address.address[11] == 0xff;
    std::vector<std::byte> bytes;
    if (address.family == net::kAfInet || mapped) {
        const auto offset = mapped ? 12U : 0U;
        for (std::size_t index = 0; index < 4; ++index)
            bytes.push_back(static_cast<std::byte>(address.address[offset + index]));
    } else {
        for (const auto value : address.address)
            bytes.push_back(static_cast<std::byte>(value));
    }
    return NewAddress(call.vm, bytes);
}

VmObjectRef SocketAddressOf(IntrinsicContext& call, const net::GuestSocketAddress& address) {
    const auto inet = InetAddressOf(call, address);
    const auto inet_root = call.vm.ProtectReferences(std::array{inet});
    const auto result = call.vm.NewIntrinsicInstance("Ljava/net/InetSocketAddress;");
    const auto result_root = call.vm.ProtectReferences(std::array{result});
    InvokeDirect(call.vm, "Ljava/net/InetSocketAddress;", "<init>", "(Ljava/net/InetAddress;I)V",
                 {VmValue::Ref(result), VmValue::Ref(inet), VmValue::Int(address.port)});
    return result;
}

// libcore fillInetSocketAddress writes the private fields directly.
void FillSocketAddress(IntrinsicContext& call, const VmObjectRef target, const net::GuestSocketAddress& address) {
    if (!target.IsValid() || address.family == net::kAfUnspec) return;
    const auto inet = InetAddressOf(call, address);
    const auto root = call.vm.ProtectReferences(std::array{inet});
    SetRefField(call.vm, target, "addr", "Ljava/net/InetAddress;", inet);
    SetIntField(call.vm, target, "port", address.port);
}

VmObjectRef NewSocketDescriptor(IntrinsicContext& call, const std::int32_t fd) {
    const auto object = call.vm.NewIntrinsicInstance("Ljava/io/FileDescriptor;");
    SetIntField(call.vm, object, "descriptor", fd);
    return object;
}

std::int32_t OptionInt(IntrinsicContext& call, const VmObjectRef fd, const std::string_view function) {
    auto& stack = RequireStack(call, function);
    std::vector<std::byte> value;
    CheckSocket(call, fd, function,
                stack.GetOption(SocketHandle(call, fd), call.arguments[1].AsInt(), call.arguments[2].AsInt(), value));
    std::uint32_t result{};
    for (std::size_t index = 0; index < std::min<std::size_t>(4, value.size()); ++index)
        result |= std::to_integer<std::uint32_t>(value[index]) << (index * 8U);
    return static_cast<std::int32_t>(result);
}

std::vector<std::byte> Words(std::initializer_list<std::int32_t> values) {
    std::vector<std::byte> bytes;
    for (const auto value : values)
        for (unsigned shift = 0; shift < 32; shift += 8)
            bytes.push_back(static_cast<std::byte>((static_cast<std::uint32_t>(value) >> shift) & 0xffU));
    return bytes;
}

void SetOption(IntrinsicContext& call, const std::string_view function, const std::vector<std::byte>& value) {
    auto& stack = RequireStack(call, function);
    const auto fd = call.arguments[0].ref;
    CheckSocket(call, fd, function,
                stack.SetOption(SocketHandle(call, fd), call.arguments[1].AsInt(), call.arguments[2].AsInt(), value));
}

void AppendPosixSocketNatives(IntrinsicClassBuilder& posix) {
    posix.VirtualMethod(
        "socket", "(III)Ljava/io/FileDescriptor;",
        [](IntrinsicContext& call) {
            auto& stack = RequireStack(call, "socket");
            const auto fd =
                stack.Socket(call.arguments[0].AsInt(), call.arguments[1].AsInt(), call.arguments[2].AsInt());
            if (fd < 0) ThrowErrno(call.vm, "socket", -fd);
            return VmValue::Ref(NewSocketDescriptor(call, fd));
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "socketpair", "(IIILjava/io/FileDescriptor;Ljava/io/FileDescriptor;)V",
        [](IntrinsicContext& call) {
            auto& stack = RequireStack(call, "socketpair");
            const auto first = IntrinsicCall(call).NonNullRef(3, "fd1");
            const auto second = IntrinsicCall(call).NonNullRef(4, "fd2");
            std::array<int, 2> pair{};
            const auto result =
                stack.SocketPair(call.arguments[0].AsInt(), call.arguments[1].AsInt(), call.arguments[2].AsInt(), pair);
            if (result < 0) ThrowErrno(call.vm, "socketpair", -result);
            SetIntField(call.vm, first, "descriptor", pair[0]);
            SetIntField(call.vm, second, "descriptor", pair[1]);
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    for (const bool connect : {false, true}) {
        posix.VirtualMethod(
            connect ? "connect" : "bind", "(Ljava/io/FileDescriptor;Ljava/net/InetAddress;I)V",
            [connect](IntrinsicContext& call) {
                const auto function = connect ? "connect" : "bind";
                auto& stack = RequireStack(call, function);
                const auto fd_ref = call.arguments[0].ref;
                const auto fd = SocketHandle(call, fd_ref);
                const auto address =
                    SockaddrOf(call, call.arguments[1].ref, call.arguments[2].AsInt(), SocketDomain(stack, fd));
                const auto result =
                    connect ? Unlocked(call, [&] { return stack.Connect(fd, address); }) : stack.Bind(fd, address);
                CheckSocket(call, fd_ref, function, result);
                return VmValue::Void();
            },
            kAccPublic | kAccNative);
    }
    posix.VirtualMethod(
        "listen", "(Ljava/io/FileDescriptor;I)V",
        [](IntrinsicContext& call) {
            auto& stack = RequireStack(call, "listen");
            const auto fd = call.arguments[0].ref;
            CheckSocket(call, fd, "listen", stack.Listen(SocketHandle(call, fd), call.arguments[1].AsInt()));
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "accept", "(Ljava/io/FileDescriptor;Ljava/net/InetSocketAddress;)Ljava/io/FileDescriptor;",
        [](IntrinsicContext& call) {
            auto& stack = RequireStack(call, "accept");
            const auto fd_ref = call.arguments[0].ref;
            const auto fd = SocketHandle(call, fd_ref);
            net::GuestSocketAddress peer;
            const auto accepted = Unlocked(call, [&] { return stack.Accept(fd, &peer, 0); });
            CheckSocket(call, fd_ref, "accept", accepted);
            const auto descriptor = NewSocketDescriptor(call, accepted);
            const auto root = call.vm.ProtectReferences(std::array{descriptor});
            FillSocketAddress(call, call.arguments[1].ref, peer);
            return VmValue::Ref(descriptor);
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "shutdown", "(Ljava/io/FileDescriptor;I)V",
        [](IntrinsicContext& call) {
            auto& stack = RequireStack(call, "shutdown");
            const auto fd = call.arguments[0].ref;
            CheckSocket(call, fd, "shutdown", stack.ShutdownSocket(SocketHandle(call, fd), call.arguments[1].AsInt()));
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    for (const bool peer : {false, true}) {
        posix.VirtualMethod(
            peer ? "getpeername" : "getsockname", "(Ljava/io/FileDescriptor;)Ljava/net/SocketAddress;",
            [peer](IntrinsicContext& call) {
                const auto function = peer ? "getpeername" : "getsockname";
                auto& stack = RequireStack(call, function);
                const auto fd = call.arguments[0].ref;
                net::GuestSocketAddress address;
                const auto handle = SocketHandle(call, fd);
                CheckSocket(call, fd, function,
                            peer ? stack.PeerName(handle, address) : stack.LocalName(handle, address));
                return VmValue::Ref(SocketAddressOf(call, address));
            },
            kAccPublic | kAccNative);
    }
    posix.DirectMethod(
        "sendtoBytes", "(Ljava/io/FileDescriptor;Ljava/lang/Object;IIILjava/net/InetAddress;I)I",
        [](IntrinsicContext& call) {
            auto& stack = RequireStack(call, "sendto");
            const auto fd_ref = call.arguments[0].ref;
            const auto fd = SocketHandle(call, fd_ref);
            const auto count =
                std::min<std::int32_t>(call.arguments[3].AsInt(), static_cast<std::int32_t>(kMaxSocketTransfer));
            const auto source =
                PosixBytes(call, call.arguments[1].ref, call.arguments[2].AsInt(), std::max(count, 0), false);
            std::vector<std::byte> bytes(source.begin(), source.end());
            std::optional<net::GuestSocketAddress> destination;
            if (call.arguments[5].ref.IsValid())
                destination =
                    SockaddrOf(call, call.arguments[5].ref, call.arguments[6].AsInt(), SocketDomain(stack, fd));
            const auto flags = call.arguments[4].AsInt();
            const auto sent = Unlocked(call, [&] {
                return stack.Send(fd, bytes, flags | net::kMsgNoSignal, destination ? &*destination : nullptr);
            });
            return VmValue::Int(static_cast<std::int32_t>(CheckSocket(call, fd_ref, "sendto", sent)));
        },
        kAccPrivate | kAccNative);
    posix.DirectMethod(
        "recvfromBytes", "(Ljava/io/FileDescriptor;Ljava/lang/Object;IIILjava/net/InetSocketAddress;)I",
        [](IntrinsicContext& call) {
            auto& stack = RequireStack(call, "recvfrom");
            const auto fd_ref = call.arguments[0].ref;
            const auto fd = SocketHandle(call, fd_ref);
            const auto requested = call.arguments[3].AsInt();
            // Validate the destination range before blocking, as JNI region access would.
            static_cast<void>(PosixBytes(call, call.arguments[1].ref, call.arguments[2].AsInt(), requested, true));
            std::vector<std::byte> bytes(
                std::min<std::size_t>(static_cast<std::size_t>(std::max(requested, 0)), kMaxSocketTransfer));
            const auto flags = call.arguments[4].AsInt();
            net::GuestSocketAddress source;
            const auto received = Unlocked(call, [&] { return stack.Receive(fd, bytes, flags, &source); });
            CheckSocket(call, fd_ref, "recvfrom", received);
            const auto copied = std::min<std::size_t>(static_cast<std::size_t>(received), bytes.size());
            auto destination = PosixBytes(call, call.arguments[1].ref, call.arguments[2].AsInt(),
                                          static_cast<std::int32_t>(copied), true);
            std::copy_n(bytes.begin(), copied, destination.begin());
            FillSocketAddress(call, call.arguments[5].ref, source);
            return VmValue::Int(static_cast<std::int32_t>(received));
        },
        kAccPrivate | kAccNative);
    posix.VirtualMethod(
        "poll", "([Llibcore/io/StructPollfd;I)I",
        [](IntrinsicContext& call) {
            auto& stack = RequireStack(call, "poll");
            auto& filesystem = stack.FileSystem();
            const auto array = IntrinsicCall(call).NonNullRef(0, "fds");
            const auto timeout_ms = call.arguments[1].AsInt();
            const auto length = call.vm.Model().ArrayLength(array);
            struct Entry final {
                VmObjectRef pollfd;
                std::int32_t fd{-1};
                std::uint16_t events{};
                std::uint16_t revents{};
            };
            std::vector<Entry> entries;
            for (std::int32_t index = 0; index < length; ++index) {
                const auto pollfd = call.vm.Model().GetObjectElement(array, static_cast<JniSize>(index));
                if (!pollfd.IsValid()) continue;
                const auto descriptor = GetRefField(call.vm, pollfd, "fd", "Ljava/io/FileDescriptor;");
                const auto events = static_cast<std::uint16_t>(GetCat1Field(call.vm, pollfd, "events", "S"));
                entries.push_back({pollfd, descriptor.IsValid() ? FileDescriptorHandle(call, descriptor) : -1, events});
            }
            // Linux poll bits; PipePollEvents: read=1, write=2, error=4, hangup=8, invalid=16.
            const auto evaluate = [&]() {
                std::int32_t ready{};
                for (auto& entry : entries) {
                    entry.revents = 0;
                    if (entry.fd < 0) continue;
                    std::uint16_t mask{};
                    try {
                        const auto bits = filesystem.PipePollEvents(entry.fd);
                        if (!bits) {
                            mask = 0x0001 | 0x0004 | 0x0040 | 0x0100;
                        } else if ((*bits & 16U) != 0U) {
                            mask = 0x0020;
                        } else {
                            if ((*bits & 1U) != 0U) mask |= 0x0001 | 0x0040;
                            if ((*bits & 2U) != 0U) mask |= 0x0004 | 0x0100;
                            if ((*bits & 4U) != 0U) mask |= 0x0008;
                            if ((*bits & 8U) != 0U) mask |= 0x0010;
                        }
                    } catch (const VfsError&) {
                        mask = 0x0020;
                    }
                    entry.revents = static_cast<std::uint16_t>(mask & (entry.events | 0x0008 | 0x0010 | 0x0020));
                    if (entry.revents != 0) ++ready;
                }
                return ready;
            };
            const auto start = std::chrono::steady_clock::now();
            const auto ready = Unlocked(call, [&] {
                for (;;) {
                    const auto generation = filesystem.PipeReadinessGeneration();
                    const auto count = evaluate();
                    if (count != 0 || timeout_ms == 0) return count;
                    auto slice = std::chrono::milliseconds(10);
                    if (timeout_ms > 0) {
                        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now() - start);
                        if (elapsed.count() >= timeout_ms) return 0;
                        slice = std::min(slice, std::chrono::milliseconds(timeout_ms) - elapsed);
                    }
                    filesystem.WaitPipeReadiness(generation, slice);
                }
            });
            for (const auto& entry : entries)
                SetShortField(call.vm, entry.pollfd, "revents", static_cast<std::int16_t>(entry.revents));
            return VmValue::Int(ready);
        },
        kAccPublic | kAccNative);

    posix.VirtualMethod(
        "getsockoptInt", "(Ljava/io/FileDescriptor;II)I",
        [](IntrinsicContext& call) { return VmValue::Int(OptionInt(call, call.arguments[0].ref, "getsockopt")); },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "getsockoptByte", "(Ljava/io/FileDescriptor;II)I",
        [](IntrinsicContext& call) {
            return VmValue::Int(OptionInt(call, call.arguments[0].ref, "getsockopt") & 0xff);
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "getsockoptLinger", "(Ljava/io/FileDescriptor;II)Llibcore/io/StructLinger;",
        [](IntrinsicContext& call) {
            auto& stack = RequireStack(call, "getsockopt");
            const auto fd = call.arguments[0].ref;
            std::vector<std::byte> value;
            CheckSocket(
                call, fd, "getsockopt",
                stack.GetOption(SocketHandle(call, fd), call.arguments[1].AsInt(), call.arguments[2].AsInt(), value));
            value.resize(8);
            const auto word = [&](const std::size_t offset) {
                std::uint32_t result{};
                for (std::size_t index = 0; index < 4; ++index)
                    result |= std::to_integer<std::uint32_t>(value[offset + index]) << (index * 8U);
                return static_cast<std::int32_t>(result);
            };
            const auto linger = call.vm.NewIntrinsicInstance("Llibcore/io/StructLinger;");
            const auto root = call.vm.ProtectReferences(std::array{linger});
            InvokeDirect(call.vm, "Llibcore/io/StructLinger;", "<init>", "(II)V",
                         {VmValue::Ref(linger), VmValue::Int(word(0)), VmValue::Int(word(4))});
            return VmValue::Ref(linger);
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "getsockoptTimeval", "(Ljava/io/FileDescriptor;II)Llibcore/io/StructTimeval;",
        [](IntrinsicContext& call) {
            auto& stack = RequireStack(call, "getsockopt");
            const auto fd = call.arguments[0].ref;
            std::vector<std::byte> value;
            CheckSocket(
                call, fd, "getsockopt",
                stack.GetOption(SocketHandle(call, fd), call.arguments[1].AsInt(), call.arguments[2].AsInt(), value));
            value.resize(8);
            std::int64_t seconds{}, micros{};
            for (std::size_t index = 0; index < 4; ++index) {
                seconds |= static_cast<std::int64_t>(std::to_integer<std::uint32_t>(value[index]) << (index * 8U));
                micros |= static_cast<std::int64_t>(std::to_integer<std::uint32_t>(value[4 + index]) << (index * 8U));
            }
            return InvokeDirect(call.vm, "Llibcore/io/StructTimeval;", "fromMillis", "(J)Llibcore/io/StructTimeval;",
                                {VmValue::Long(seconds * 1000 + micros / 1000)});
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "getsockoptUcred", "(Ljava/io/FileDescriptor;II)Llibcore/io/StructUcred;",
        [](IntrinsicContext& call) {
            auto& stack = RequireStack(call, "getsockopt");
            const auto fd = call.arguments[0].ref;
            std::vector<std::byte> value;
            CheckSocket(
                call, fd, "getsockopt",
                stack.GetOption(SocketHandle(call, fd), call.arguments[1].AsInt(), call.arguments[2].AsInt(), value));
            value.resize(12);
            const auto word = [&](const std::size_t offset) {
                std::uint32_t result{};
                for (std::size_t index = 0; index < 4; ++index)
                    result |= std::to_integer<std::uint32_t>(value[offset + index]) << (index * 8U);
                return static_cast<std::int32_t>(result);
            };
            const auto ucred = call.vm.NewIntrinsicInstance("Llibcore/io/StructUcred;");
            const auto root = call.vm.ProtectReferences(std::array{ucred});
            InvokeDirect(call.vm, "Llibcore/io/StructUcred;", "<init>", "(III)V",
                         {VmValue::Ref(ucred), VmValue::Int(word(0)), VmValue::Int(word(4)), VmValue::Int(word(8))});
            return VmValue::Ref(ucred);
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "getsockoptInAddr", "(Ljava/io/FileDescriptor;II)Ljava/net/InetAddress;",
        [](IntrinsicContext& call) {
            // IP_MULTICAST_IF: no interface was selected (INADDR_ANY).
            static_cast<void>(RequireStack(call, "getsockopt"));
            return VmValue::Ref(NewAddress(call.vm, std::array<std::byte, 4>{}));
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "setsockoptInt", "(Ljava/io/FileDescriptor;III)V",
        [](IntrinsicContext& call) {
            SetOption(call, "setsockopt", Words({call.arguments[3].AsInt()}));
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "setsockoptByte", "(Ljava/io/FileDescriptor;III)V",
        [](IntrinsicContext& call) {
            SetOption(call, "setsockopt", Words({call.arguments[3].AsInt() & 0xff}));
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "setsockoptLinger", "(Ljava/io/FileDescriptor;IILlibcore/io/StructLinger;)V",
        [](IntrinsicContext& call) {
            const auto linger = IntrinsicCall(call).NonNullRef(3, "linger");
            SetOption(
                call, "setsockopt",
                Words({GetCat1Field(call.vm, linger, "l_onoff", "I"), GetCat1Field(call.vm, linger, "l_linger", "I")}));
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "setsockoptTimeval", "(Ljava/io/FileDescriptor;IILlibcore/io/StructTimeval;)V",
        [](IntrinsicContext& call) {
            const auto timeval = IntrinsicCall(call).NonNullRef(3, "timeval");
            SetOption(call, "setsockopt",
                      Words({static_cast<std::int32_t>(GetLongField(call.vm, timeval, "tv_sec")),
                             static_cast<std::int32_t>(GetLongField(call.vm, timeval, "tv_usec"))}));
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "setsockoptIpMreqn", "(Ljava/io/FileDescriptor;III)V",
        [](IntrinsicContext& call) {
            // IP_MULTICAST_IF by interface index; stored without egress effect.
            SetOption(call, "setsockopt", Words({call.arguments[3].AsInt()}));
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "setsockoptGroupReq", "(Ljava/io/FileDescriptor;IILlibcore/io/StructGroupReq;)V",
        [](IntrinsicContext& call) -> VmValue {
            static_cast<void>(RequireStack(call, "setsockopt"));
            if (auto* ledger = call.vm.Ledger()) ledger->RecordUnimplemented("dexvm.posix.multicast_group", 0);
            ThrowErrno(call.vm, "setsockopt", kEnodev, "multicast group membership is not supported");
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "setsockoptIfreq", "(Ljava/io/FileDescriptor;IILjava/lang/String;)V",
        [](IntrinsicContext& call) -> VmValue {
            // SO_BINDTODEVICE needs CAP_NET_RAW, which an application does not hold.
            static_cast<void>(RequireStack(call, "setsockopt"));
            ThrowErrno(call.vm, "setsockopt", 1);
        },
        kAccPublic | kAccNative);

    posix.VirtualMethod(
        "getaddrinfo", "(Ljava/lang/String;Llibcore/io/StructAddrinfo;)[Ljava/net/InetAddress;",
        [](IntrinsicContext& call) {
            const auto host_ref = call.arguments[0].ref;
            const auto hints = call.arguments[1].ref;
            if (!host_ref.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "node == null"};
            const auto host = call.vm.StringUtf8(host_ref);
            const auto flags = hints.IsValid() ? GetCat1Field(call.vm, hints, "ai_flags", "I") : 0;
            const auto family = hints.IsValid() ? GetCat1Field(call.vm, hints, "ai_family", "I") : 0;
            const auto numeric = net::ParseNumericAddress(host);
            net::GuestResolveResult result;
            if (numeric) {
                result.found = true;
                result.addresses.push_back(*numeric);
            } else if ((flags & kAiNumericHost) != 0) {
                ThrowGai(call.vm, "getaddrinfo", kEaiNoName,
                         "getaddrinfo failed: " + std::string(Api19GaiStrerror(kEaiNoName)));
            } else if (auto* stack = call.vm.Network().Stack()) {
                result = Unlocked(call, [&] { return stack->Resolve(host); });
            }
            const bool online =
                call.vm.Network().Stack() && call.vm.Network().Stack()->Mode() == net::NetworkMode::online;
            std::vector<net::GuestSocketAddress> addresses;
            for (const auto& address : result.addresses) {
                if (family != 0 && address.family != family) continue;
                // AI_ADDRCONFIG: the emulated device has an IPv4 address only when online.
                if ((flags & kAiAddrConfig) != 0 && !numeric && !net::IsLoopback(address) &&
                    (address.family == net::kAfInet6 || !online))
                    continue;
                addresses.push_back(address);
            }
            // Bionic's proxied getaddrinfo reports every lookup failure as EAI_NODATA.
            if (addresses.empty())
                ThrowGai(call.vm, "getaddrinfo", kEaiNoData,
                         "getaddrinfo failed: " + std::string(Api19GaiStrerror(kEaiNoData)));
            const auto array = call.vm.Model().NewObjectArray(
                call.vm.Linker().ResolveDescriptor("[Ljava/net/InetAddress;"),
                call.vm.Linker().ResolveDescriptor("Ljava/net/InetAddress;"), static_cast<JniSize>(addresses.size()));
            const auto roots = call.vm.ProtectReferences(std::array{array, host_ref});
            for (std::size_t index = 0; index < addresses.size(); ++index) {
                const auto& address = addresses[index];
                std::vector<std::byte> bytes;
                const auto size = address.family == net::kAfInet ? 4U : 16U;
                for (std::size_t offset = 0; offset < size; ++offset)
                    bytes.push_back(static_cast<std::byte>(address.address[offset]));
                call.vm.Model().SetObjectElement(array, static_cast<JniSize>(index),
                                                 NewAddress(call.vm, bytes, numeric ? VmObjectRef{0} : host_ref));
            }
            return VmValue::Ref(array);
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "getnameinfo", "(Ljava/net/InetAddress;I)Ljava/lang/String;",
        [](IntrinsicContext& call) {
            const auto numeric_text = NumericAddress(call.vm, call.arguments[0].ref);
            // bionic netdb.h.
            constexpr std::int32_t kNiNumericHost = 2;
            constexpr std::int32_t kNiNameReqd = 4;
            const auto flags = call.arguments[1].AsInt();
            if ((flags & kNiNumericHost) != 0) return VmValue::Ref(call.vm.NewStringUtf8(numeric_text));
            std::optional<std::string> name;
            if (auto* stack = call.vm.Network().Stack()) {
                if (const auto parsed = net::ParseNumericAddress(numeric_text))
                    name = Unlocked(call, [&] { return stack->ReverseLookup(*parsed); });
            }
            if (name) return VmValue::Ref(call.vm.NewStringUtf8(*name));
            if ((flags & kNiNameReqd) != 0)
                ThrowGai(call.vm, "getnameinfo", kEaiNoName,
                         "getnameinfo failed: " + std::string(Api19GaiStrerror(kEaiNoName)));
            return VmValue::Ref(call.vm.NewStringUtf8(numeric_text));
        },
        kAccPublic | kAccNative);
}

void AppendAddressNatives(std::vector<IntrinsicClassDecl>& catalog, const CoreIntrinsicServices& services) {
    auto constants =
        IntrinsicClassBuilder::Class("Llibcore/io/OsConstants;", "Ljava/lang/Object;", {}, kAccPublic | kAccFinal);
    constants.MergesBootClass();
    std::vector<std::pair<IntrinsicFieldHandle, std::int32_t>> fields;
    fields.reserve(kApi19OsConstants.size());
    for (const auto& value : kApi19OsConstants)
        fields.emplace_back(
            constants.BoundStaticField(std::string(value.name), "I", kAccPublic | kAccStatic | kAccFinal), value.value);
    constants.StaticMethod(
        "initConstants", "()V",
        [fields = std::move(fields)](IntrinsicContext& call) {
            const IntrinsicCall c(call);
            for (const auto& [field, value] : fields)
                c.SetInt(field, value);
            return VmValue::Void();
        },
        kAccPrivate | kAccStatic | kAccNative);
    catalog.push_back(std::move(constants).Build());

    auto posix = IntrinsicClassBuilder::Class("Llibcore/io/Posix;", "Ljava/lang/Object;", {"Llibcore/io/Os;"},
                                              kAccPublic | kAccFinal);
    posix.MergesBootClass();
#include "api19_posix_natives.inc"
    posix.VirtualMethod(
        "waitpid", "(ILlibcore/util/MutableInt;I)I",
        [wait = services.wait_process](IntrinsicContext& call) -> VmValue {
            if (!wait) ThrowErrno(call.vm, "waitpid", 10);  // API 19 ECHILD
            constexpr std::int32_t kWaitNoHang = 1;
            const auto pid = call.arguments[0].AsInt();
            const auto status_holder = call.arguments[1].ref;
            const auto no_hang = (call.arguments[2].AsInt() & kWaitNoHang) != 0;
            std::int32_t status{};
            std::int32_t result{};
            const auto depth = call.vm.ExecutionLock().ReleaseForBlocking();
            try {
                result = wait(pid, no_hang, status);
            } catch (...) {
                call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
                throw;
            }
            call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
            if (result < 0) ThrowErrno(call.vm, "waitpid", -result);
            if (result > 0 && status_holder.IsValid()) SetIntField(call.vm, status_holder, "value", status);
            return VmValue::Int(result);
        },
        kAccPublic | kAccNative);
    if (services.kill_process) {
        posix.VirtualMethod(
            "kill", "(II)V",
            [kill = services.kill_process](IntrinsicContext& call) -> VmValue {
                const auto result = kill(call.arguments[0].AsInt(), call.arguments[1].AsInt());
                if (result < 0) ThrowErrno(call.vm, "kill", -result);
                return VmValue::Void();
            },
            kAccPublic | kAccNative);
    }
    posix.VirtualMethod(
        "getenv", "(Ljava/lang/String;)Ljava/lang/String;",
        [read = services.process_environment](IntrinsicContext& call) {
            const auto name = IntrinsicCall(call).NonNullRef(0, "name");
            if (!read)
                throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "guest environment is unavailable"};
            const auto key = call.vm.StringUtf8(name);
            if (key.empty() || key.find('=') != std::string::npos) return VmValue::Ref(VmObjectRef{});
            const auto prefix = key + "=";
            for (const auto& entry : read()) {
                if (entry.starts_with(prefix)) return VmValue::Ref(call.vm.NewStringUtf8(entry.substr(prefix.size())));
            }
            return VmValue::Ref(VmObjectRef{});
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "environ", "()[Ljava/lang/String;",
        [read = services.process_environment](IntrinsicContext& call) {
            if (!read)
                throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "guest environment is unavailable"};
            return VmValue::Ref(detail::NewStringArray(call.vm, read()));
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "getpwuid", "(I)Llibcore/io/StructPasswd;",
        [read = services.guest_passwd](IntrinsicContext& call) {
            if (!read) ThrowErrno(call.vm, "getpwuid_r", 38);
            const auto passwd = read(call.vm, call.arguments[0].AsInt());
            if (!passwd) ThrowErrno(call.vm, "getpwuid_r", 2);
            const auto result = call.vm.NewIntrinsicInstance("Llibcore/io/StructPasswd;");
            const auto result_root = call.vm.ProtectReferences(std::array{result});
            const auto name = call.vm.NewStringUtf8(passwd->name);
            const auto name_root = call.vm.ProtectReferences(std::array{name});
            const auto directory = call.vm.NewStringUtf8(passwd->directory);
            const auto directory_root = call.vm.ProtectReferences(std::array{directory});
            const auto shell = call.vm.NewStringUtf8(passwd->shell);
            InvokeDirect(call.vm, "Llibcore/io/StructPasswd;", "<init>",
                         "(Ljava/lang/String;IILjava/lang/String;Ljava/lang/String;)V",
                         {VmValue::Ref(result), VmValue::Ref(name), VmValue::Int(passwd->uid),
                          VmValue::Int(passwd->gid), VmValue::Ref(directory), VmValue::Ref(shell)});
            return VmValue::Ref(result);
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "getppid", "()I", [](IntrinsicContext&) { return VmValue::Int(kGuestParentProcessId); },
        kAccPublic | kAccNative);
    if (services.process_id)
        posix.VirtualMethod(
            "getpid", "()I", [identity = services.process_id](IntrinsicContext&) { return VmValue::Int(identity()); },
            kAccPublic | kAccNative);
    if (services.process_uid)
        posix.VirtualMethod(
            "getuid", "()I", [identity = services.process_uid](IntrinsicContext&) { return VmValue::Int(identity()); },
            kAccPublic | kAccNative);
    if (services.current_thread_id)
        posix.VirtualMethod(
            "gettid", "()I",
            [identity = services.current_thread_id](IntrinsicContext& call) { return VmValue::Int(identity(call.vm)); },
            kAccPublic | kAccNative);

    posix.VirtualMethod("sysconf", "(I)J", [cores = services.cpu_cores](IntrinsicContext& call) {
        const auto selector = call.arguments[0].AsInt();
        if (selector == 39 || selector == 40) return VmValue::Long(kApi19MmapPageSize);
        if (selector == 96 || selector == 97) return VmValue::Long(cores);
        if (auto* ledger = call.vm.Ledger())
            ledger->RecordUnimplemented("dexvm.posix.sysconf." + std::to_string(selector), 0);
        throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                          "Posix.sysconf selector is not implemented: " + std::to_string(selector)};
    });
    posix.VirtualMethod(
        "mmap", "(JJIILjava/io/FileDescriptor;J)J",
        [provider = services.memory_mappings](IntrinsicContext& call) {
            const auto address = memory::GuestAddress{MappingWord(call, 0, "mmap")};
            const auto size = MappingWord(call, 1, "mmap");
            const auto offset = call.arguments[5].AsLong();
            if (offset < 0 || offset % kApi19MmapPageSize != 0) ThrowErrno(call.vm, "mmap", 22);
            const auto pages = offset / kApi19MmapPageSize;
            if (pages > std::numeric_limits<std::uint32_t>::max()) ThrowErrno(call.vm, "mmap", 75);
            const auto fd = FileDescriptorHandle(call, call.arguments[4].ref);
            const auto* mappings = provider ? provider() : nullptr;
            const auto result =
                mappings && mappings->map
                    ? mappings->map({address, size, static_cast<std::uint32_t>(call.arguments[2].AsInt()),
                                     static_cast<std::uint32_t>(call.arguments[3].AsInt()), fd,
                                     static_cast<std::uint32_t>(pages)})
                    : -38;
            return VmValue::Long(CheckMappingResult(call.vm, "mmap", result));
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "munmap", "(JJ)V",
        [provider = services.memory_mappings](IntrinsicContext& call) {
            const auto address = memory::GuestAddress{MappingWord(call, 0, "munmap")};
            const auto size = MappingWord(call, 1, "munmap");
            const auto* mappings = provider ? provider() : nullptr;
            CheckMappingResult(call.vm, "munmap", mappings && mappings->unmap ? mappings->unmap(address, size) : -38);
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "msync", "(JJI)V",
        [provider = services.memory_mappings, thread_id = services.current_thread_id](IntrinsicContext& call) {
            const auto address = memory::GuestAddress{MappingWord(call, 0, "msync")};
            const auto size = MappingWord(call, 1, "msync");
            const auto flags = static_cast<std::uint32_t>(call.arguments[2].AsInt());
            const auto* mappings = provider ? provider() : nullptr;
            const auto tid = thread_id ? static_cast<std::uint64_t>(thread_id(call.vm)) : call.vm.CurrentContextToken();
            CheckMappingResult(call.vm, "msync",
                               mappings && mappings->sync ? mappings->sync(address, size, flags, tid) : -38);
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "open", "(Ljava/lang/String;II)Ljava/io/FileDescriptor;",
        [](IntrinsicContext& call) {
            const auto path = call.vm.StringUtf8(call.arguments[0].ref);
            const auto flags = call.arguments[1].AsInt();
            const auto access = flags & 3;
            const bool readable = access == 0 || access == 2;
            const bool writable = access == 1 || access == 2;
            const bool create = (flags & 64) != 0;
            const bool exclusive = (flags & 128) != 0;
            const bool truncate = (flags & 512) != 0;
            const bool append = (flags & 1024) != 0;
            try {
                auto file = call.vm.IO().OpenFile(path, readable, writable, append, truncate, create, exclusive);
                const auto descriptor = call.vm.NewIntrinsicInstance("Ljava/io/FileDescriptor;");
                call.vm.IO().SetDescriptor(descriptor,
                                           {IoRuntime::DescriptorKind::vfs_path, path, 0, false, {}, {}, file});
                call.vm.IO().BindFileStream(descriptor, file, false);
                SetIntField(call.vm, descriptor, "descriptor", file->handle);
                return VmValue::Ref(descriptor);
            } catch (const IoRuntimeError& error) {
                ThrowErrno(call.vm, "open", error.ErrorNumber(), error.what());
            }
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "close", "(Ljava/io/FileDescriptor;)V",
        [](IntrinsicContext& call) {
            const auto fd = call.arguments[0].ref;
            if (const auto* state = call.vm.IO().FindDescriptor(fd);
                state && state->kind == IoRuntime::DescriptorKind::interface_query) {
                call.vm.IO().CloseDescriptor(fd);
                SetIntField(call.vm, fd, "descriptor", -1);
                return VmValue::Void();
            }
            try {
                static_cast<void>(PosixDescriptor(call, fd, "close"));
            } catch (const VmJavaThrow&) {
                if (fd.IsValid()) SetIntField(call.vm, fd, "descriptor", -1);
                throw;
            }
            SetIntField(call.vm, fd, "descriptor", -1);
            try {
                call.vm.IO().CloseDescriptor(fd);
            } catch (const IoRuntimeError& error) {
                ThrowErrno(call.vm, "close", error.ErrorNumber(), error.what());
            }
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "dup", "(Ljava/io/FileDescriptor;)Ljava/io/FileDescriptor;",
        [](IntrinsicContext& call) {
            const auto original = call.arguments[0].ref;
            static_cast<void>(PosixDescriptor(call, original, "dup"));
            const auto fd = call.vm.NewIntrinsicInstance("Ljava/io/FileDescriptor;");
            const auto root = call.vm.ProtectReferences(std::array{fd});
            try {
                call.vm.IO().DuplicateDescriptor(original, fd);
                SetIntField(call.vm, fd, "descriptor", call.vm.IO().Descriptor(fd).file->handle);
                return VmValue::Ref(fd);
            } catch (const IoRuntimeError& error) {
                ThrowErrno(call.vm, "dup", error.ErrorNumber(), error.what());
            }
        },
        kAccPublic | kAccNative);
    // IoUtils.setBlocking uses F_GETFL/F_SETFL: O_NONBLOCK changes go to the
    // shared open description; other status changes are unsupported.
    for (const bool with_argument : {false, true}) {
        posix.VirtualMethod(
            with_argument ? "fcntlLong" : "fcntlVoid",
            with_argument ? "(Ljava/io/FileDescriptor;IJ)I" : "(Ljava/io/FileDescriptor;I)I",
            [with_argument](IntrinsicContext& call) {
                const auto fd = call.arguments[0].ref;
                static_cast<void>(PosixDescriptor(call, fd, "fcntl"));
                const auto cmd = call.arguments[1].AsInt();
                try {
                    const auto flags = call.vm.IO().DescriptorFlags(fd);
                    if (!with_argument && cmd == 3) return VmValue::Int(flags);
                    if (with_argument && cmd == 4) {
                        const auto requested = static_cast<std::int32_t>(call.arguments[2].AsLong());
                        if (requested == flags) return VmValue::Int(0);
                        // Linux F_SETFL ignores the access mode; only O_NONBLOCK may change here.
                        constexpr std::int32_t kNonBlocking = 2048;
                        auto* stack = call.vm.Network().Stack();
                        if (stack && ((requested ^ flags) & ~(kNonBlocking | 3)) == 0) {
                            stack->FileSystem().SetNonBlocking(FileDescriptorHandle(call, fd),
                                                               (requested & kNonBlocking) != 0);
                            return VmValue::Int(0);
                        }
                    }
                } catch (const VfsError& error) {
                    ThrowErrno(call.vm, "fcntl", error.ErrorNumber(), error.what());
                } catch (const IoRuntimeError& error) {
                    ThrowErrno(call.vm, "fcntl", error.ErrorNumber(), error.what());
                }
                if (auto* ledger = call.vm.Ledger())
                    ledger->RecordUnimplemented("dexvm.posix.fcntl." + std::to_string(cmd), 0);
                ThrowErrno(call.vm, "fcntl", 38, "fcntl selector or status change is unsupported");
            },
            kAccPublic | kAccNative);
    }
    // POSIX record locks belong to the process and never conflict with the
    // owner's own locks. A sandbox runs exactly one guest process (the
    // instance running lock), so F_SETLK/F_SETLKW always acquire and F_GETLK
    // reports F_UNLCK, after the kernel's argument and access-mode checks.
    posix.VirtualMethod(
        "fcntlFlock", "(Ljava/io/FileDescriptor;ILlibcore/io/StructFlock;)I",
        [](IntrinsicContext& call) {
            const auto fd = call.arguments[0].ref;
            static_cast<void>(PosixDescriptor(call, fd, "fcntl"));
            const auto cmd = call.arguments[1].AsInt();
            const auto lock = call.arguments[2].ref;
            if (!lock.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "flock == null"};
            constexpr std::int32_t kGetLk = 5, kSetLk = 6, kSetLkW = 7;
            constexpr std::int32_t kGetLk64 = 12, kSetLk64 = 13, kSetLkW64 = 14;
            constexpr std::int16_t kRdLck = 0, kWrLck = 1, kUnLck = 2;
            const bool query = cmd == kGetLk || cmd == kGetLk64;
            if (!query && cmd != kSetLk && cmd != kSetLkW && cmd != kSetLk64 && cmd != kSetLkW64)
                ThrowErrno(call.vm, "fcntl", 22, "fcntlFlock command is not a record-lock command");
            const auto type = static_cast<std::int16_t>(GetCat1Field(call.vm, lock, "l_type", "S"));
            const auto whence = static_cast<std::int16_t>(GetCat1Field(call.vm, lock, "l_whence", "S"));
            const auto start = GetLongField(call.vm, lock, "l_start");
            const auto length = GetLongField(call.vm, lock, "l_len");
            if ((type != kRdLck && type != kWrLck && type != kUnLck) || (query && type == kUnLck) || whence < 0 ||
                whence > 2)
                ThrowErrno(call.vm, "fcntl", 22, "invalid flock type or whence");
            try {
                std::int64_t base = 0;
                if (whence == 1) base = static_cast<std::int64_t>(call.vm.IO().FileOffset(fd));
                if (whence == 2) base = static_cast<std::int64_t>(call.vm.IO().FileSize(fd));
                // Kernel flock_to_posix_lock: EOVERFLOW past off_t, EINVAL before 0.
                constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
                if (start > 0 && base > kMax - start) throw IoRuntimeError("flock range overflows", 75);
                auto first = base + start;
                if (first < 0) throw IoRuntimeError("flock range starts before the file", 22);
                if (length > 0 && first > kMax - (length - 1)) throw IoRuntimeError("flock range overflows", 75);
                if (length < 0) {
                    if (length == std::numeric_limits<std::int64_t>::min())
                        throw IoRuntimeError("flock range starts before the file", 22);
                    first += length;
                }
                if (first < 0) throw IoRuntimeError("flock range starts before the file", 22);
                const auto access = call.vm.IO().DescriptorFlags(fd) & 3;
                if (!query && ((type == kRdLck && access == 1) || (type == kWrLck && access == 0)))
                    throw IoRuntimeError("flock type does not match descriptor access mode", 9);
            } catch (const IoRuntimeError& error) {
                ThrowErrno(call.vm, "fcntl", error.ErrorNumber(), error.what());
            }
            if (query) SetShortField(call.vm, lock, "l_type", kUnLck);
            return VmValue::Int(0);
        },
        kAccPublic | kAccNative);
    const auto read_bytes = [](IntrinsicContext& call) {
        const auto fd = call.arguments[0].ref;
        const auto handle = FileDescriptorHandle(call, fd);
        if (handle == 1 || handle == 2) ThrowErrno(call.vm, "read", 9);
        if (handle != 0) static_cast<void>(PosixDescriptor(call, fd, "read"));
        const auto buffer = call.arguments[1].ref;
        const auto offset = call.arguments[2].AsInt();
        const auto count = call.arguments[3].AsInt();
        auto bytes = PosixBytes(call, buffer, offset, count, true);
        try {
            if (handle == 0) return VmValue::Int(0);
            WaitForPipe(call, fd, false);
            const auto amount = call.vm.IO().ReadFileStream(fd, bytes);
            if (amount == 0) return VmValue::Int(-1);
            return VmValue::Int(static_cast<std::int32_t>(amount));
        } catch (const IoRuntimeError& error) {
            ThrowErrno(call.vm, "read", error.ErrorNumber(), error.what());
        }
    };
    posix.DirectMethod("readBytes", "(Ljava/io/FileDescriptor;Ljava/lang/Object;II)I", read_bytes,
                       kAccPrivate | kAccNative);
    posix.DirectMethod(
        "writeBytes", "(Ljava/io/FileDescriptor;Ljava/lang/Object;II)I",
        [write = services.write_standard_stream](IntrinsicContext& call) {
            const auto fd = call.arguments[0].ref;
            const auto handle = FileDescriptorHandle(call, fd);
            if (handle == 0) ThrowErrno(call.vm, "write", 9);
            const bool standard = handle == 1 || handle == 2;
            if (!standard) static_cast<void>(PosixDescriptor(call, fd, "write"));
            const auto buffer = call.arguments[1].ref;
            const auto offset = call.arguments[2].AsInt();
            const auto count = call.arguments[3].AsInt();
            const auto bytes = PosixBytes(call, buffer, offset, count, false);
            try {
                if (!standard) {
                    WaitForPipe(call, fd, true);
                    call.vm.IO().WriteFileStream(fd, bytes);
                } else if (!bytes.empty()) {
                    const bool error = handle == 2;
                    if (const auto& console = call.vm.Console(error)) {
                        console(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
                    } else if (write) {
                        write(call.vm, error, bytes);
                    } else if (auto* log = call.vm.Log()) {
                        log->Write(error ? core::LogLevel::error : core::LogLevel::info,
                                   error ? "guest.stderr" : "guest.stdout",
                                   std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
                    }
                }
                return VmValue::Int(count);
            } catch (const IoRuntimeError& error) {
                ThrowErrno(call.vm, "write", error.ErrorNumber(), error.what());
            }
        },
        kAccPrivate | kAccNative);
    posix.VirtualMethod(
        "lseek", "(Ljava/io/FileDescriptor;JI)J",
        [](IntrinsicContext& call) {
            const auto fd = call.arguments[0].ref;
            static_cast<void>(PosixDescriptor(call, fd, "lseek"));
            const auto offset = call.arguments[1].AsLong();
            const auto whence = call.arguments[2].AsInt();
            try {
                if (whence < 0 || whence > 2) throw IoRuntimeError("invalid seek whence", 22);
                std::uint64_t target{};
                if (whence == 0) {
                    if (offset < 0) throw IoRuntimeError("negative seek", 22);
                    target = static_cast<std::uint64_t>(offset);
                } else {
                    const auto base = whence == 1 ? call.vm.IO().FileOffset(fd) : call.vm.IO().FileSize(fd);
                    if (offset < 0 && static_cast<std::uint64_t>(-offset) > base)
                        throw IoRuntimeError("negative seek", 22);
                    target = offset < 0 ? base - static_cast<std::uint64_t>(-offset)
                                        : base + static_cast<std::uint64_t>(offset);
                }
                call.vm.IO().SetFileOffset(fd, target);
                return VmValue::Long(static_cast<std::int64_t>(target));
            } catch (const IoRuntimeError& error) {
                ThrowErrno(call.vm, "lseek", error.ErrorNumber(), error.what());
            }
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "fstat", "(Ljava/io/FileDescriptor;)Llibcore/io/StructStat;",
        [](IntrinsicContext& call) {
            static_cast<void>(PosixDescriptor(call, call.arguments[0].ref, "fstat"));
            try {
                return VmValue::Ref(NewStructStat(call, call.vm.IO().DescriptorInfo(call.arguments[0].ref)));
            } catch (const IoRuntimeError& error) {
                ThrowErrno(call.vm, "fstat", error.ErrorNumber(), error.what());
            }
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "statvfs", "(Ljava/lang/String;)Llibcore/io/StructStatVfs;",
        [](IntrinsicContext& call) {
            const auto path_ref = IntrinsicCall(call).NonNullRef(0, "path");
            try {
                const auto stats = call.vm.IO().StatVfs(call.vm.StringUtf8(path_ref));
                const auto& v = stats.values;
                for (auto value : v)
                    if (value > static_cast<std::uint64_t>(INT64_MAX))
                        throw IoRuntimeError("filesystem statistic exceeds API19 long", 75);
                if (!v[0] || !v[1] || v[4] > v[3] || v[3] > v[2] || v[7] > v[6] || v[6] > v[5] ||
                    v[2] > static_cast<std::uint64_t>(INT64_MAX) / v[0])
                    throw IoRuntimeError("filesystem statistics are inconsistent", 5);
                const auto type = call.vm.Linker().ResolveDescriptor("Llibcore/io/StructStatVfs;");
                const auto initialized = call.vm.EnsureClassInitialized(type);
                if (initialized.exception.IsValid())
                    throw VmJavaThrow{call.vm.Linker().Class(initialized.exception_class).descriptor,
                                      initialized.exception_message, initialized.exception};
                const auto object = call.vm.NewIntrinsicInstance("Llibcore/io/StructStatVfs;");
                const auto root = call.vm.ProtectReferences(std::array{object});
                std::vector<VmValue> arguments{VmValue::Ref(object)};
                for (auto value : v)
                    arguments.push_back(VmValue::Long(static_cast<std::int64_t>(value)));
                InvokeDirect(call.vm, "Llibcore/io/StructStatVfs;", "<init>", "(JJJJJJJJJJJ)V", std::move(arguments));
                return VmValue::Ref(object);
            } catch (const IoRuntimeError& error) {
                if ((error.ErrorNumber() == 38 || error.ErrorNumber() == 95) && call.vm.Ledger())
                    call.vm.Ledger()->RecordUnimplemented("runtime.vfs_filesystem_stats", 0);
                ThrowErrno(call.vm, "statvfs", error.ErrorNumber(), error.what());
            }
        },
        kAccPublic | kAccNative);
    for (const auto* name : {"stat", "lstat"}) {
        posix.VirtualMethod(
            name, "(Ljava/lang/String;)Llibcore/io/StructStat;",
            [name](IntrinsicContext& call) {
                if (!call.vm.IO().HasFileSystem()) ThrowErrno(call.vm, name, 38);
                const auto path = call.vm.StringUtf8(call.arguments[0].ref);
                const auto info = call.vm.IO().Stat(path);
                if (!info) ThrowErrno(call.vm, name, 2, "path not found");
                return VmValue::Ref(NewStructStat(call, *info));
            },
            kAccPublic | kAccNative);
    }
    posix.VirtualMethod(
        "ftruncate", "(Ljava/io/FileDescriptor;J)V",
        [](IntrinsicContext& call) {
            static_cast<void>(PosixDescriptor(call, call.arguments[0].ref, "ftruncate"));
            const auto size = call.arguments[1].AsLong();
            if (size < 0) ThrowErrno(call.vm, "ftruncate", 22);
            try {
                call.vm.IO().SetFileSize(call.arguments[0].ref, static_cast<std::uint64_t>(size));
            } catch (const IoRuntimeError& error) {
                ThrowErrno(call.vm, "ftruncate", error.ErrorNumber(), error.what());
            }
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    for (const auto* name : {"fsync", "fdatasync"}) {
        posix.VirtualMethod(
            name, "(Ljava/io/FileDescriptor;)V",
            [name](IntrinsicContext& call) {
                static_cast<void>(PosixDescriptor(call, call.arguments[0].ref, name));
                try {
                    call.vm.IO().SyncDescriptor(call.arguments[0].ref);
                } catch (const IoRuntimeError& error) {
                    ThrowErrno(call.vm, name, error.ErrorNumber(), error.what());
                }
                return VmValue::Void();
            },
            kAccPublic | kAccNative);
    }
    posix.VirtualMethod(
        "chmod", "(Ljava/lang/String;I)V",
        [](IntrinsicContext& call) {
            const auto path = IntrinsicCall(call).NonNullRef(0, "path");
            try {
                call.vm.IO().Chmod(call.vm.StringUtf8(path), static_cast<std::uint32_t>(call.arguments[1].AsInt()));
                return VmValue::Void();
            } catch (const IoRuntimeError& error) {
                ThrowErrno(call.vm, "chmod", error.ErrorNumber(), error.what());
            }
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "fchmod", "(Ljava/io/FileDescriptor;I)V",
        [](IntrinsicContext& call) {
            const auto fd = call.arguments[0].ref;
            static_cast<void>(PosixDescriptor(call, fd, "fchmod"));
            try {
                call.vm.IO().Fchmod(fd, static_cast<std::uint32_t>(call.arguments[1].AsInt()));
                return VmValue::Void();
            } catch (const IoRuntimeError& error) {
                ThrowErrno(call.vm, "fchmod", error.ErrorNumber(), error.what());
            }
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "access", "(Ljava/lang/String;I)Z",
        [](IntrinsicContext& call) {
            if (!call.vm.IO().HasFileSystem()) ThrowErrno(call.vm, "access", 38);
            const auto mode = call.arguments[1].AsInt();
            if ((mode & ~7) != 0) ThrowErrno(call.vm, "access", 22);
            const auto info = call.vm.IO().Stat(call.vm.StringUtf8(call.arguments[0].ref));
            return VmValue::Int(info.has_value() && ((mode & 2) == 0 || info->writable) &&
                                ((mode & 1) == 0 || info->is_directory));
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "mkdir", "(Ljava/lang/String;I)V",
        [](IntrinsicContext& call) {
            try {
                call.vm.IO().MakeDirectory(call.vm.StringUtf8(call.arguments[0].ref));
            } catch (const IoRuntimeError& error) {
                ThrowErrno(call.vm, "mkdir", error.ErrorNumber(), error.what());
            }
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "remove", "(Ljava/lang/String;)V",
        [](IntrinsicContext& call) {
            try {
                call.vm.IO().Delete(call.vm.StringUtf8(call.arguments[0].ref));
            } catch (const IoRuntimeError& error) {
                ThrowErrno(call.vm, "remove", error.ErrorNumber(), error.what());
            }
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "rename", "(Ljava/lang/String;Ljava/lang/String;)V",
        [](IntrinsicContext& call) {
            try {
                call.vm.IO().Rename(call.vm.StringUtf8(call.arguments[0].ref),
                                    call.vm.StringUtf8(call.arguments[1].ref));
            } catch (const IoRuntimeError& error) {
                ThrowErrno(call.vm, "rename", error.ErrorNumber(), error.what());
            }
            return VmValue::Void();
        },
        kAccPublic | kAccNative);
    for (const bool write : {false, true}) {
        posix.DirectMethod(
            write ? "pwriteBytes" : "preadBytes", "(Ljava/io/FileDescriptor;Ljava/lang/Object;IIJ)I",
            [write](IntrinsicContext& call) {
                const auto fd = call.arguments[0].ref;
                const auto function = write ? "pwrite" : "pread";
                static_cast<void>(PosixDescriptor(call, fd, function));
                const auto offset = call.arguments[4].AsLong();
                if (offset < 0) ThrowErrno(call.vm, function, 22);
                auto bytes = PosixBytes(call, call.arguments[1].ref, call.arguments[2].AsInt(),
                                        call.arguments[3].AsInt(), !write);
                try {
                    const auto amount = write ? call.vm.IO().WriteFileAt(fd, static_cast<std::uint64_t>(offset), bytes)
                                              : call.vm.IO().ReadFileAt(fd, static_cast<std::uint64_t>(offset), bytes);
                    return VmValue::Int(static_cast<std::int32_t>(amount));
                } catch (const IoRuntimeError& error) {
                    ThrowErrno(call.vm, function, error.ErrorNumber(), error.what());
                }
            },
            kAccPrivate | kAccNative);
    }
    AppendPosixSocketNatives(posix);
    posix.VirtualMethod(
        "ioctlInt", "(Ljava/io/FileDescriptor;ILlibcore/util/MutableInt;)I",
        [](IntrinsicContext& call) {
            try {
                const auto fd = call.arguments[0].ref;
                const auto available = FileDescriptorHandle(call, fd) == 0 ? 0 : call.vm.IO().FileAvailable(fd);
                SetIntField(call.vm, call.arguments[2].ref, "value", static_cast<std::int32_t>(available));
                return VmValue::Int(0);
            } catch (const IoRuntimeError& error) {
                ThrowErrno(call.vm, "ioctl", error.ErrorNumber(), error.what());
            }
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "strerror", "(I)Ljava/lang/String;",
        [](IntrinsicContext& call) {
            return VmValue::Ref(call.vm.NewStringUtf8(Api19Strerror(call.arguments[0].AsInt())));
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "isatty", "(Ljava/io/FileDescriptor;)Z", [](IntrinsicContext&) { return VmValue::Int(0); },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "ioctlInetAddress", "(Ljava/io/FileDescriptor;ILjava/lang/String;)Ljava/net/InetAddress;",
        [interfaces = services.network_interfaces](IntrinsicContext& call) {
            const auto* state = call.vm.IO().FindDescriptor(call.arguments[0].ref);
            const auto cmd = call.arguments[1].AsInt();
            auto* stack = call.vm.Network().Stack();
            const bool socket =
                (state && !state->closed && state->kind == IoRuntime::DescriptorKind::interface_query) ||
                (stack && call.arguments[0].ref.IsValid() &&
                 stack->IsSocket(FileDescriptorHandle(call, call.arguments[0].ref)));
            if (!socket || (cmd != 0x8915 && cmd != 0x8919 && cmd != 0x891b) || !interfaces) {
                if (auto* ledger = call.vm.Ledger())
                    ledger->RecordUnimplemented("libcore.io.Posix.ioctlInetAddress", 0);
                ThrowErrno(call.vm, "ioctl", 38);
            }
            const auto name = call.vm.StringUtf8(call.arguments[2].ref);
            const auto& table = interfaces(call.vm);
            const auto found =
                std::find_if(table.begin(), table.end(), [&name](const auto& entry) { return entry.name == name; });
            if (found == table.end()) ThrowErrno(call.vm, "ioctl", 19);
            if (!found->ipv4) ThrowErrno(call.vm, "ioctl", 99);
            const auto& bytes = cmd == 0x8915 ? *found->ipv4 : cmd == 0x8919 ? found->broadcast : found->netmask;
            return VmValue::Ref(NewAddress(call.vm, bytes));
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "inet_pton", "(ILjava/lang/String;)Ljava/net/InetAddress;",
        [](IntrinsicContext& call) {
            const auto parsed = ParseAddress(call.vm.StringUtf8(call.arguments[1].ref));
            return VmValue::Ref(parsed ? NewAddress(call.vm, *parsed) : VmObjectRef{0});
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "uname", "()Llibcore/io/StructUtsname;",
        [](IntrinsicContext& call) {
            const auto& identity = call.vm.Network().Policy().host_identity;
            if (!identity)
                throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;", "guest host identity is not configured"};
            const auto result = call.vm.NewIntrinsicInstance("Llibcore/io/StructUtsname;");
            const auto result_root = call.vm.ProtectReferences(std::array{result});
            const auto sysname = call.vm.NewStringUtf8(identity->sysname);
            const auto sysname_root = call.vm.ProtectReferences(std::array{sysname});
            const auto nodename = call.vm.NewStringUtf8(identity->nodename);
            const auto nodename_root = call.vm.ProtectReferences(std::array{nodename});
            const auto release = call.vm.NewStringUtf8(identity->release);
            const auto release_root = call.vm.ProtectReferences(std::array{release});
            const auto version = call.vm.NewStringUtf8(identity->version);
            const auto version_root = call.vm.ProtectReferences(std::array{version});
            const auto machine = call.vm.NewStringUtf8(identity->machine);
            const auto machine_root = call.vm.ProtectReferences(std::array{machine});
            InvokeDirect(
                call.vm, "Llibcore/io/StructUtsname;", "<init>",
                "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
                {VmValue::Ref(result), VmValue::Ref(sysname), VmValue::Ref(nodename), VmValue::Ref(release),
                 VmValue::Ref(version), VmValue::Ref(machine)});
            return VmValue::Ref(result);
        },
        kAccPublic | kAccNative);
    posix.VirtualMethod(
        "gai_strerror", "(I)Ljava/lang/String;",
        [](IntrinsicContext& call) {
            return VmValue::Ref(call.vm.NewStringUtf8(Api19GaiStrerror(call.arguments[0].AsInt())));
        },
        kAccPublic | kAccNative);
    catalog.push_back(std::move(posix).Build());
}

using namespace detail;

[[nodiscard]] std::string JavaUtf8(Interpreter& vm, const VmObjectRef value) {
    if (!value.IsValid()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "null string"};
    const auto units = vm.Model().StringChars(value);
    auto utf8 = core::Utf16ToUtf8(std::span{units}, core::InvalidUtf16Policy::replace, '?');
    return utf8.has_value() ? std::move(*utf8) : std::string();
}

[[nodiscard]] std::optional<std::vector<std::string>> JavaStrings(Interpreter& vm, const VmObjectRef array) {
    if (!array.IsValid()) return std::nullopt;
    std::vector<std::string> values;
    const auto length = vm.Model().ArrayLength(array);
    values.reserve(static_cast<std::size_t>(length));
    for (std::int32_t index = 0; index < length; ++index)
        values.push_back(JavaUtf8(vm, vm.Model().GetObjectElement(array, static_cast<JniSize>(index))));
    return values;
}

[[nodiscard]] const char* ExecErrorText(const std::int32_t error) {
    switch (error) {
    case 2: return "No such file or directory";
    case 7: return "Argument list too long";
    case 11: return "Try again";
    case 13: return "Permission denied";
    default: return "I/O error";
    }
}

}  // namespace

IntrinsicClassDecl Declare_java_lang_ProcessManager_Natives(const CoreIntrinsicServices& services) {
    auto builder = IntrinsicClassBuilder::Class("Ljava/lang/ProcessManager;", "Ljava/lang/Object;", {}, kAccFinal);
    builder.MergesBootClass();
    builder.StaticMethod(
        "exec",
        "([Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;Ljava/io/FileDescriptor;Ljava/io/FileDescriptor;"
        "Ljava/io/FileDescriptor;Z)I",
        [spawn = services.spawn_process](IntrinsicContext& context) -> VmValue {
            auto& vm = context.vm;
            const auto command = JavaStrings(vm, context.arguments[0].ref);
            if (!command || command->empty()) throw VmJavaThrow{"Ljava/lang/NullPointerException;", "command"};
            std::int32_t result = -2;
            CoreIntrinsicServices::SpawnedProcess spawned;
            if (spawn) {
                const auto environment = JavaStrings(vm, context.arguments[1].ref);
                std::optional<std::string> directory;
                if (context.arguments[2].ref.IsValid()) directory = JavaUtf8(vm, context.arguments[2].ref);
                result = spawn(*command, environment, directory, context.arguments[6].AsInt() != 0, spawned);
            }
            if (result < 0) {
                if (auto* ledger = vm.Ledger()) ledger->RecordUnimplemented("dexvm.process.exec", 0);
                if (auto* log = vm.Log()) {
                    constexpr std::size_t kMaxCommandBytes = 128;
                    log->Write(core::LogLevel::warn, "dexvm.process.exec", "Child process executable is unavailable",
                               {}, {{"command", command->front().substr(0, kMaxCommandBytes)}});
                }
                throw VmJavaThrow{"Ljava/io/IOException;", ExecErrorText(-result)};
            }
            SetIntField(vm, context.arguments[3].ref, "descriptor", spawned.stdout_read);
            SetIntField(vm, context.arguments[4].ref, "descriptor", spawned.stdin_write);
            SetIntField(vm, context.arguments[5].ref, "descriptor", spawned.stderr_read);
            return VmValue::Int(result);
        },
        kAccPrivate | kAccStatic | kAccNative);
    return std::move(builder).Build();
}

void AppendJavaNetPlatform(std::vector<IntrinsicClassDecl>& catalog, const CoreIntrinsicServices& services) {
    catalog.push_back(Declare_java_net_HttpURLConnection_Shell());
    catalog.push_back(Declare_java_net_URL_Class());
    auto connection = Declare_java_net_URLConnection_Fields();
    catalog.push_back(std::move(connection.declaration));
    catalog.push_back(
        Declare_org_ogplay_io_ClasspathURLConnection_Class(services, connection.url, connection.connected));
    catalog.push_back(Declare_java_net_URLEncoder_Class());
    catalog.push_back(Declare_java_net_URLDecoder_Class());
    AppendAddressNatives(catalog, services);
    // IoBridge.closeSocket wakes threads blocked on the descriptor. Closing a
    // guest socket already fails its blocked operations, so nothing else is owed.
    auto monitor = IntrinsicClassBuilder::Class("Llibcore/io/AsynchronousCloseMonitor;", "Ljava/lang/Object;", {},
                                                kAccPublic | kAccFinal);
    monitor.MergesBootClass();
    monitor.StaticMethod(
        "signalBlockedThreads", "(Ljava/io/FileDescriptor;)V", [](IntrinsicContext&) { return VmValue::Void(); },
        kAccPublic | kAccStatic | kAccNative);
    catalog.push_back(std::move(monitor).Build());
}
}  // namespace ogplay::runtime::dexvm::intrinsics
