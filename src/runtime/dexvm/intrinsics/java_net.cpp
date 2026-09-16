// DVM-80: API-family translation unit. Physical consolidation only.

// ---- migrated from java_net_MalformedURLException.cpp ----
#include "catalog.h"
#include "shared.h"
#include "api19_os_constants.h"
#include "ogplay/runtime/dexvm/intrinsic_builder.h"

// ---- migrated from dexvm_android java.net / javax.net.ssl ----

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include <boost/url/encode.hpp>
#include <boost/url/grammar/lut_chars.hpp>
#include <boost/url/pct_string_view.hpp>

#include "ogplay/core/text.h"
#include "ogplay/runtime/dexvm/io_runtime.h"

namespace ogplay::runtime::dexvm::intrinsics {
    namespace {
        VmValue InvokeDirect(Interpreter& vm, const char* owner,
                             const char* name, const char* signature,
                             std::vector<VmValue> arguments);
        [[nodiscard]] const LinkedField& ObjectField(
            Interpreter& vm, const VmObjectRef object,
            const std::string& name, const std::string& descriptor) {
            const auto field = vm.Linker().FindFieldRecursive(
                vm.Model().ObjectClass(object), name, descriptor);
            if (!field) throw DexVmError{
                DexVmErrorReason::internal_invariant,
                "missing BootDex field: " + name};
            return vm.Linker().Field(*field);
        }

        void SetIntField(Interpreter& vm, const VmObjectRef object,
                         const std::string& name, const std::int32_t value) {
            const auto& field = ObjectField(vm, object, name, "I");
            vm.Model().InstanceSlots(object)[field.slot] = {
                static_cast<std::uint32_t>(value), SlotTag::cat1};
        }

        [[noreturn]] void ThrowErrno(Interpreter& vm,
                                     const std::string_view function,
                                     const std::int32_t error_number,
                                     const std::string_view detail = {}) {
            const auto function_ref = vm.NewStringUtf8(function);
            const auto roots = vm.ProtectReferences(std::array{function_ref});
            const auto exception = vm.NewIntrinsicInstance(
                "Llibcore/io/ErrnoException;");
            const auto exception_root = vm.ProtectReferences(std::array{exception});
            InvokeDirect(vm, "Llibcore/io/ErrnoException;", "<init>",
                         "(Ljava/lang/String;I)V",
                         {VmValue::Ref(exception), VmValue::Ref(function_ref),
                          VmValue::Int(error_number)});
            throw VmJavaThrow{"Llibcore/io/ErrnoException;",
                              detail.empty() ? std::string(function) + " failed"
                                             : std::string(detail),
                              exception};
        }

        [[nodiscard]] IoRuntime::DescriptorState& PosixDescriptor(
            IntrinsicContext& call, const VmObjectRef descriptor,
            const std::string_view function) {
            try { return call.vm.IO().Descriptor(descriptor); }
            catch (const IoRuntimeError& error) {
                ThrowErrno(call.vm, function, error.ErrorNumber(), error.what());
            }
        }

        [[nodiscard]] VmObjectRef NewStructStat(IntrinsicContext& call,
                                                const IoFileInfo& info) {
            const auto result = call.vm.NewIntrinsicInstance("Llibcore/io/StructStat;");
            const auto root = call.vm.ProtectReferences(std::array{result});
            constexpr std::int32_t kRegular = 0100000 | 0644;
            constexpr std::int32_t kDirectory = 0040000 | 0755;
            InvokeDirect(call.vm, "Llibcore/io/StructStat;", "<init>",
                "(JJIJIIJJJJJJJ)V",
                {VmValue::Ref(result), VmValue::Long(0), VmValue::Long(0),
                 VmValue::Int(info.is_directory ? kDirectory : kRegular),
                 VmValue::Long(1), VmValue::Int(0), VmValue::Int(0),
                 VmValue::Long(0), VmValue::Long(static_cast<std::int64_t>(info.size)),
                 VmValue::Long(0), VmValue::Long(0), VmValue::Long(0),
                 VmValue::Long(4096), VmValue::Long(
                     static_cast<std::int64_t>((info.size + 511U) / 512U))});
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

        [[noreturn]] void UriSyntax(const std::string_view spec,
                                    const std::string_view reason) {
            throw VmJavaThrow{
                "Ljava/net/URISyntaxException;",
                std::string(reason) + ": " + std::string(spec)
            };
        }

        void ValidateUriComponent(const std::string_view spec,
                                  const std::string_view component) {
            const auto hex = [](const unsigned char value) {
                return std::isxdigit(value) != 0;
            };
            for (std::size_t index = 0; index < component.size(); ++index) {
                const auto value = static_cast<unsigned char>(component[index]);
                if (value <= 0x20U || value == 0x7fU)
                    UriSyntax(spec, "Illegal character in URI");
                if (value == '%') {
                    if (index + 2U >= component.size() ||
                        !hex(static_cast<unsigned char>(component[index + 1U])) ||
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
            const auto main_end = fragment_start == std::string_view::npos
                                      ? view.size()
                                      : fragment_start;
            if (fragment_start != std::string_view::npos) {
                result.fragment = std::string(view.substr(fragment_start + 1U));
                ValidateUriComponent(view, *result.fragment);
            }

            std::size_t start{};
            const auto colon = view.substr(0, main_end).find(':');
            const auto first_delimiter = view.substr(0, main_end).find_first_of("/?");
            if (colon != std::string_view::npos &&
                (first_delimiter == std::string_view::npos || colon < first_delimiter)) {
                if (colon == 0U ||
                    std::isalpha(static_cast<unsigned char>(view[0])) == 0) {
                    UriSyntax(view, "Invalid URI scheme");
                }
                for (std::size_t index = 1; index < colon; ++index) {
                    const auto value = static_cast<unsigned char>(view[index]);
                    if (std::isalnum(value) == 0 && value != '+' && value != '-' &&
                        value != '.') {
                        UriSyntax(view, "Invalid URI scheme");
                    }
                }
                result.absolute = true;
                result.scheme = std::string(view.substr(0, colon));
                start = colon + 1U;
                if (start == main_end)
                    UriSyntax(view, "Scheme-specific part expected");
            }

            result.scheme_specific_part =
                    std::string(view.substr(start, main_end - start));
            ValidateUriComponent(view, result.scheme_specific_part);
            result.opaque = result.absolute &&
                            (result.scheme_specific_part.empty() ||
                             result.scheme_specific_part.front() != '/');
            if (result.opaque) return result;

            std::size_t path_start = start;
            if (start + 1U < main_end && view.substr(start, 2U) == "//") {
                const auto authority_start = start + 2U;
                auto authority_end = view.find_first_of("/?", authority_start);
                if (authority_end == std::string_view::npos || authority_end > main_end)
                    authority_end = main_end;
                if (authority_start == main_end)
                    UriSyntax(view, "Authority expected");
                if (authority_start < authority_end) {
                    result.authority = std::string(
                        view.substr(authority_start, authority_end - authority_start));
                    ValidateUriComponent(view, *result.authority);
                }
                path_start = authority_end;
            }
            auto query_start = view.find('?', path_start);
            if (query_start == std::string_view::npos || query_start > main_end)
                query_start = main_end;
            result.path = std::string(view.substr(path_start, query_start - path_start));
            ValidateUriComponent(view, *result.path);
            if (query_start < main_end) {
                result.query = std::string(
                    view.substr(query_start + 1U, main_end - query_start - 1U));
                ValidateUriComponent(view, *result.query);
            }
            return result;
        }

        IntrinsicClassDecl DeclarePlatformHttpURLConnection() {
            return std::move(IntrinsicClassBuilder::Class(
                        "Ljava/net/HttpURLConnection;",
                        "Ljava/net/URLConnection;"))
                    .Build();
        }

        IntrinsicClassDecl DeclareProxySelector() {
            auto builder = IntrinsicClassBuilder::Class(
                "Ljava/net/ProxySelector;", "Ljava/lang/Object;", {},
                kAccPublic | kAccAbstract);
            builder.Constructor("()V", NoopVoid());
            builder.StaticMethod(
                "getDefault", "()Ljava/net/ProxySelector;",
                [](IntrinsicContext&) {
                    // OGPlay has no process-wide proxy service. Apache's API 19
                    // route planner treats a null selector as a direct route.
                    return VmValue::Ref(VmObjectRef{});
                });
            builder.UnimplementedStatic(
                "setDefault", "(Ljava/net/ProxySelector;)V");
            builder.UnimplementedVirtual(
                "select", "(Ljava/net/URI;)Ljava/util/List;");
            builder.UnimplementedVirtual(
                "connectFailed",
                "(Ljava/net/URI;Ljava/net/SocketAddress;Ljava/io/IOException;)V");
            return std::move(builder).Build();
        }

        [[noreturn]] void MalformedUrl(const std::string_view spec,
                                       const std::string_view reason) {
            throw VmJavaThrow{
                "Ljava/net/MalformedURLException;",
                std::string(reason) + ": " + std::string(spec)
            };
        }

        IntrinsicClassDecl DeclarePlatformUrl() {
            auto builder = IntrinsicClassBuilder::Class("Ljava/net/URL;",
                "Ljava/lang/Object;", {"Ljava/io/Serializable;"},
                kAccPublic | kAccFinal);
            const auto protocol = builder.BoundInstanceField(
                "protocol", "Ljava/lang/String;", kAccPrivate);
            const auto authority = builder.BoundInstanceField(
                "authority", "Ljava/lang/String;", kAccPrivate);
            const auto host = builder.BoundInstanceField(
                "host", "Ljava/lang/String;", kAccPrivate);
            const auto port = builder.BoundInstanceField(
                "port", "I", kAccPrivate);
            const auto file = builder.BoundInstanceField(
                "file", "Ljava/lang/String;", kAccPrivate);
            const auto ref = builder.BoundInstanceField(
                "ref", "Ljava/lang/String;", kAccPrivate);
            const auto user_info = builder.BoundInstanceField(
                "userInfo", "Ljava/lang/String;", kAccPrivate | kAccTransient);
            const auto path = builder.BoundInstanceField(
                "path", "Ljava/lang/String;", kAccPrivate | kAccTransient);
            const auto query = builder.BoundInstanceField(
                "query", "Ljava/lang/String;", kAccPrivate | kAccTransient);
            builder.Constructor("(Ljava/lang/String;)V",
                [=](IntrinsicContext& context) {
                    IntrinsicCall call(context);
                    const auto input = call.Ref(0);
                    if (!input.IsValid()) MalformedUrl({}, "URL spec is null");
                    const auto input_text = call.Vm().StringUtf8(input);
                    const std::string trimmed(
                        core::TrimAsciiWhitespace(input_text));
                    ParsedUri parsed;
                    try {
                        parsed = ParseUri(std::string(trimmed));
                    } catch (const VmJavaThrow&) {
                        MalformedUrl(trimmed, "Invalid URL");
                    }
                    if (!parsed.absolute || !parsed.scheme.has_value()) {
                        MalformedUrl(trimmed, "Protocol not found");
                    }
                    if (!parsed.authority.has_value() &&
                        parsed.scheme_specific_part.starts_with("//")) {
                        parsed.authority = std::string{};
                    }
                    auto scheme = *parsed.scheme;
                    std::transform(scheme.begin(), scheme.end(), scheme.begin(),
                        [](const unsigned char value) {
                            return static_cast<char>(std::tolower(value));
                        });
                    if (scheme != "http" && scheme != "https" &&
                        scheme != "file") {
                        MalformedUrl(trimmed, "Unknown protocol");
                    }

                    std::optional<std::string> parsed_user_info;
                    std::string parsed_host;
                    std::int32_t parsed_port = -1;
                    if (parsed.authority.has_value()) {
                        auto host_and_port = std::string_view(*parsed.authority);
                        const auto user_end = host_and_port.rfind('@');
                        if (user_end != std::string_view::npos) {
                            parsed_user_info =
                                std::string(host_and_port.substr(0, user_end));
                            host_and_port.remove_prefix(user_end + 1U);
                        }

                        std::string_view port_view;
                        if (!host_and_port.empty() &&
                            host_and_port.front() == '[') {
                            const auto close = host_and_port.find(']');
                            if (close == std::string_view::npos) {
                                MalformedUrl(trimmed, "Invalid IPv6 host");
                            }
                            parsed_host = std::string(
                                host_and_port.substr(0, close + 1U));
                            if (close + 1U < host_and_port.size()) {
                                if (host_and_port[close + 1U] != ':') {
                                    MalformedUrl(trimmed, "Invalid authority");
                                }
                                port_view = host_and_port.substr(close + 2U);
                            }
                        } else {
                            const auto separator = host_and_port.rfind(':');
                            if (separator != std::string_view::npos) {
                                parsed_host = std::string(
                                    host_and_port.substr(0, separator));
                                port_view = host_and_port.substr(separator + 1U);
                            } else {
                                parsed_host = std::string(host_and_port);
                            }
                        }

                        if (!port_view.empty()) {
                            std::uint32_t value{};
                            const auto result = std::from_chars(
                                port_view.data(),
                                port_view.data() + port_view.size(), value);
                            if (result.ec != std::errc{} ||
                                result.ptr != port_view.data() + port_view.size() ||
                                value > UINT16_MAX) {
                                MalformedUrl(trimmed, "Invalid port");
                            }
                            parsed_port = static_cast<std::int32_t>(value);
                        } else if (!host_and_port.empty() &&
                                   host_and_port.back() == ':') {
                            MalformedUrl(trimmed, "Invalid port");
                        }
                    }
                    if ((scheme == "http" || scheme == "https") &&
                        (!parsed.authority.has_value() || parsed_host.empty())) {
                        MalformedUrl(trimmed, "Host expected");
                    }

                    const auto optional_ref = [&](
                        const std::optional<std::string>& value) {
                        return value.has_value()
                                   ? call.Vm().NewStringUtf8(*value)
                                   : VmObjectRef{};
                    };
                    const auto path_text = parsed.path.value_or("");
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
                return [field](IntrinsicContext& context) {
                    return VmValue::Ref(IntrinsicCall(context).GetRef(field));
                };
            };
            builder.FinalMethod("getProtocol", "()Ljava/lang/String;",
                                raw(protocol));
            builder.FinalMethod("getAuthority", "()Ljava/lang/String;",
                                raw(authority));
            builder.FinalMethod("getUserInfo", "()Ljava/lang/String;",
                                raw(user_info));
            builder.FinalMethod("getHost", "()Ljava/lang/String;", raw(host));
            builder.FinalMethod("getPort", "()I",
                [port](IntrinsicContext& context) {
                    return VmValue::Int(IntrinsicCall(context).GetInt(port));
                });
            builder.FinalMethod("getDefaultPort", "()I",
                [protocol](IntrinsicContext& context) {
                    IntrinsicCall call(context);
                    const auto value = call.Vm().StringUtf8(
                        call.GetRef(protocol));
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
                std::string rendered = call.Vm().StringUtf8(
                    call.GetRef(protocol));
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
            builder.FinalMethod("toExternalForm", "()Ljava/lang/String;",
                                external_form);
            builder.FinalOverrideMethod("toString", "()Ljava/lang/String;",
                                        external_form);

            const auto open_connection = [host, protocol](IntrinsicContext& context)
                -> VmValue {
                IntrinsicCall call(context);
                const auto protocol_text = call.Vm().StringUtf8(
                    call.GetRef(protocol));
                if (protocol_text == "file") {
                    throw VmJavaThrow{
                        "Ljava/io/IOException;",
                        "file URL I/O is not implemented"
                    };
                }
                const auto host_text = call.Vm().StringUtf8(call.GetRef(host));
                if (!call.Vm().Network().Policy().enabled) {
                    throw VmJavaThrow{
                        "Ljava/net/UnknownHostException;",
                        "network policy is offline for " + host_text
                    };
                }
                const char* descriptor = protocol_text == "https"
                    ? "Lorg/ogplay/security/OgPlayHttpsURLConnection;"
                    : protocol_text == "http"
                        ? "Lorg/ogplay/security/OgPlayHttpURLConnection;"
                        : nullptr;
                if (descriptor == nullptr) {
                    throw VmJavaThrow{
                        "Ljava/lang/UnsupportedOperationException;",
                        "URLConnection protocol is not implemented"
                    };
                }
                auto connection = call.Vm().NewIntrinsicInstance(descriptor);
                const auto roots = call.Vm().ProtectReferences(
                    std::array{connection, call.Receiver()});
                InvokeDirect(call.Vm(), descriptor, "<init>",
                             "(Ljava/net/URL;)V",
                             {VmValue::Ref(connection),
                              VmValue::Ref(call.Receiver())});
                return VmValue::Ref(connection);
            };
            builder.FinalMethod("openConnection", "()Ljava/net/URLConnection;",
                                open_connection);
            builder.FinalMethod("openStream", "()Ljava/io/InputStream;",
                                open_connection);
            return std::move(builder).Build();
        }

        IntrinsicClassDecl DeclarePlatformUrlConnection() {
            return std::move(IntrinsicClassBuilder::Class(
                        "Ljava/net/URLConnection;", "Ljava/lang/Object;"))
                    .Build();
        }

        constexpr boost::urls::grammar::lut_chars kFormUrlEncodedSafe{
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789.-*_"
        };

        void RequireUtf8Charset(IntrinsicCall& call, const std::size_t argument) {
            auto charset = call.Vm().StringUtf8(
                call.NonNullRef(argument, "charsetName"));
            for (auto& byte: charset) {
                byte = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(byte)));
            }
            if (charset != "UTF-8" && charset != "UTF8") {
                throw VmJavaThrow{"Ljava/io/UnsupportedEncodingException;", charset};
            }
        }

        [[nodiscard]] std::u16string Utf8ToUtf16Replacing(
            const std::string_view input) {
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
                    const auto byte =
                            static_cast<std::uint8_t>(input[index + offset]);
                    if ((byte & 0xc0U) != 0x80U) {
                        valid = false;
                    } else {
                        code_point = (code_point << 6U) | (byte & 0x3fU);
                    }
                }
                const auto minimum = count == 2U
                                         ? 0x80U
                                         : count == 3U
                                               ? 0x800U
                                               : count == 4U
                                                     ? 0x10000U
                                                     : 0U;
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
                    result.push_back(
                        static_cast<char16_t>(0xd800U + (code_point >> 10U)));
                    result.push_back(
                        static_cast<char16_t>(0xdc00U + (code_point & 0x3ffU)));
                }
                index += count;
            }
            return result;
        }

        [[nodiscard]] VmObjectRef FormEncode(IntrinsicCall& call,
                                             const VmObjectRef input) {
            const auto units = call.Vm().Model().StringValue(input);
            const auto utf8 = core::Utf16ToUtf8(
                std::span{units}, core::InvalidUtf16Policy::replace, '?');
            if (!utf8.has_value()) {
                throw VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;",
                    "invalid UTF-16 input"
                };
            }
            boost::urls::encoding_opts options;
            options.space_as_plus = true;
            return call.Vm().NewStringUtf8(
                boost::urls::encode(*utf8, kFormUrlEncodedSafe, options));
        }

        [[nodiscard]] VmObjectRef FormDecode(IntrinsicCall& call,
                                             const VmObjectRef input) {
            const auto units = call.Vm().Model().StringValue(input);
            if (std::find(units.begin(), units.end(), u'%') == units.end() &&
                std::find(units.begin(), units.end(), u'+') == units.end()) {
                return input;
            }
            const auto encoded = core::Utf16ToUtf8(
                std::span{units}, core::InvalidUtf16Policy::replace, '?');
            if (!encoded.has_value()) {
                throw VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;",
                    "invalid UTF-16 input"
                };
            }
            const auto parsed = boost::urls::make_pct_string_view(*encoded);
            if (!parsed.has_value()) {
                throw VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;",
                    "invalid percent escape"
                };
            }
            boost::urls::encoding_opts options;
            options.space_as_plus = true;
            return call.Vm().Model().NewString(
                Utf8ToUtf16Replacing(parsed->decode(options)));
        }

        IntrinsicHandler FormCodec(const bool encode, const bool named_charset) {
            return [encode, named_charset](IntrinsicContext& context) {
                IntrinsicCall call(context);
                const auto input = call.NonNullRef(0, "input");
                if (named_charset) RequireUtf8Charset(call, 1U);
                return VmValue::Ref(encode
                                        ? FormEncode(call, input)
                                        : FormDecode(call, input));
            };
        }

        IntrinsicClassDecl DeclarePlatformUrlEncoder() {
            auto builder = IntrinsicClassBuilder::Class("Ljava/net/URLEncoder;",
                                                        "Ljava/lang/Object;");
            builder.Constructor("()V", NoopVoid(), kAccPrivate);
            builder.StaticMethod("encode", "(Ljava/lang/String;)Ljava/lang/String;",
                                 FormCodec(true, false));
            builder.StaticMethod(
                "encode", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                FormCodec(true, true));
            return std::move(builder).Build();
        }

        IntrinsicClassDecl DeclarePlatformUrlDecoder() {
            auto builder = IntrinsicClassBuilder::Class("Ljava/net/URLDecoder;",
                                                        "Ljava/lang/Object;");
            builder.Constructor("()V", NoopVoid());
            builder.StaticMethod("decode", "(Ljava/lang/String;)Ljava/lang/String;",
                                 FormCodec(false, false));
            builder.StaticMethod(
                "decode", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;",
                FormCodec(false, true));
            return std::move(builder).Build();
        }

        [[noreturn]] void ThrowNetwork(const NetworkRuntimeError& error);
        NetworkRuntime::Endpoint EndpointFrom(IntrinsicContext& call,
                                              VmObjectRef address,
                                              std::int32_t port);

        VmValue InvokeDirect(Interpreter& vm, const char* owner, const char* name,
                             const char* signature, std::vector<VmValue> arguments) {
            const auto method = vm.Linker().FindDirectMethod(
                vm.Linker().ResolveDescriptor(owner), name, signature);
            if (!method) throw DexVmError(DexVmErrorReason::unresolved_reference,
                                          std::string(owner) + "->" + name + signature);
            const auto outcome = vm.Call(*method, arguments);
            if (outcome.exception.IsValid()) throw VmJavaThrow{
                vm.Linker().Class(outcome.exception_class).descriptor,
                outcome.exception_message, outcome.exception};
            return outcome.value;
        }

        VmObjectRef NewAddress(Interpreter& vm, std::span<const std::byte> bytes,
                               VmObjectRef host = VmObjectRef{0}) {
            const auto array = vm.Model().NewPrimitiveArray(
                vm.Linker().ResolveDescriptor("[B"), JniPrimitiveKind::byte,
                static_cast<JniSize>(bytes.size()));
            vm.Model().WriteByteRegion(array, 0, bytes);
            const auto roots = vm.ProtectReferences(std::array{array, host});
            return InvokeDirect(vm, "Ljava/net/InetAddress;", "getByAddress",
                                "(Ljava/lang/String;[B)Ljava/net/InetAddress;",
                                {VmValue::Ref(host), VmValue::Ref(array)}).ref;
        }

        std::optional<std::vector<std::byte>> ParseAddress(const std::string_view value) {
            auto ipv4 = [](const std::string_view input)
                    -> std::optional<std::array<std::byte, 4>> {
                std::array<std::byte, 4> bytes{};
                std::size_t offset{};
                for (std::size_t part = 0; part < 4; ++part) {
                    const auto end = part == 3 ? input.size() : input.find('.', offset);
                    if (end == std::string_view::npos || end == offset) return std::nullopt;
                    unsigned value{};
                    const auto [last, error] = std::from_chars(input.data() + offset,
                                                               input.data() + end, value);
                    if (error != std::errc{} || last != input.data() + end || value > 255)
                        return std::nullopt;
                    bytes[part] = static_cast<std::byte>(value);
                    offset = end + 1;
                }
                if (offset != input.size() + 1) return std::nullopt;
                return bytes;
            };
            if (const auto parsed = ipv4(value))
                return std::vector<std::byte>(parsed->begin(), parsed->end());

            std::string_view text = value;
            if (text.size() > 2 && text.front() == '[' && text.back() == ']')
                text = text.substr(1, text.size() - 2);
            if (text.find(':') == std::string_view::npos) return std::nullopt;
            auto parse_words = [&ipv4](std::string_view input,
                                       std::vector<std::uint16_t>& output) {
                if (input.empty()) return true;
                std::size_t offset{};
                while (offset < input.size()) {
                    const auto end = input.find(':', offset);
                    const auto token = input.substr(offset, end == std::string_view::npos
                                                                ? input.size() - offset : end - offset);
                    if (token.empty()) return false;
                    if (token.find('.') != std::string_view::npos) {
                        const auto tail = ipv4(token);
                        if (!tail) return false;
                        output.push_back(static_cast<std::uint16_t>(
                            (std::to_integer<unsigned>((*tail)[0]) << 8) |
                            std::to_integer<unsigned>((*tail)[1])));
                        output.push_back(static_cast<std::uint16_t>(
                            (std::to_integer<unsigned>((*tail)[2]) << 8) |
                            std::to_integer<unsigned>((*tail)[3])));
                    } else {
                        if (token.size() > 4) return false;
                        unsigned word{};
                        const auto [last, error] = std::from_chars(
                            token.data(), token.data() + token.size(), word, 16);
                        if (error != std::errc{} || last != token.data() + token.size()) return false;
                        output.push_back(static_cast<std::uint16_t>(word));
                    }
                    if (end == std::string_view::npos) break;
                    offset = end + 1;
                }
                return true;
            };
            const auto compression = text.find("::");
            if (compression != std::string_view::npos &&
                text.find("::", compression + 2) != std::string_view::npos) return std::nullopt;
            std::vector<std::uint16_t> left, right;
            if (compression == std::string_view::npos) {
                if (!parse_words(text, left) || left.size() != 8) return std::nullopt;
            } else {
                if (!parse_words(text.substr(0, compression), left) ||
                    !parse_words(text.substr(compression + 2), right) ||
                    left.size() + right.size() >= 8) return std::nullopt;
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
            const auto bytes = vm.Model().ReadByteRegion(
                bytes_ref, 0, vm.Model().ArrayLength(bytes_ref));
            if (bytes.size() == 4) {
                return std::to_string(std::to_integer<unsigned>(bytes[0])) + "." +
                       std::to_string(std::to_integer<unsigned>(bytes[1])) + "." +
                       std::to_string(std::to_integer<unsigned>(bytes[2])) + "." +
                       std::to_string(std::to_integer<unsigned>(bytes[3]));
            }
            if (bytes.size() == 16) {
                std::array<std::uint16_t, 8> words{};
                for (std::size_t i = 0; i < words.size(); ++i)
                    words[i] = static_cast<std::uint16_t>(
                        (std::to_integer<unsigned>(bytes[i * 2]) << 8) |
                        std::to_integer<unsigned>(bytes[i * 2 + 1]));
                std::size_t best_start = 8, best_length{};
                for (std::size_t i = 0; i < 8;) {
                    if (words[i] != 0) { ++i; continue; }
                    const auto start = i;
                    while (i < 8 && words[i] == 0) ++i;
                    if (i - start > best_length && i - start >= 2) {
                        best_start = start; best_length = i - start;
                    }
                }
                std::string result;
                for (std::size_t i = 0; i < 8;) {
                    if (i == best_start) {
                        result += "::"; i += best_length;
                    } else {
                        if (!result.empty() && result.back() != ':') result += ':';
                        char buffer[5]{};
                        const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), words[i], 16);
                        result.append(buffer, converted.ptr); ++i;
                    }
                }
                return result;
            }
            throw DexVmError(DexVmErrorReason::internal_invariant,
                             "InetAddress has invalid byte length");
        }

        [[noreturn]] void ThrowGai(Interpreter& vm, const std::string_view function,
                                   const std::string_view message) {
            const auto function_ref = vm.NewStringUtf8(function);
            const auto function_root = vm.ProtectReferences(std::array{function_ref});
            const auto exception = vm.NewIntrinsicInstance("Llibcore/io/GaiException;");
            const auto exception_root = vm.ProtectReferences(std::array{exception});
            InvokeDirect(vm, "Llibcore/io/GaiException;", "<init>",
                         "(Ljava/lang/String;I)V",
                         {VmValue::Ref(exception), VmValue::Ref(function_ref), VmValue::Int(8)});
            throw VmJavaThrow{"Llibcore/io/GaiException;", std::string(message), exception};
        }

        void AppendAddressNatives(std::vector<IntrinsicClassDecl>& catalog) {
            auto constants = IntrinsicClassBuilder::Class(
                "Llibcore/io/OsConstants;", "Ljava/lang/Object;", {}, kAccPublic | kAccFinal);
            std::vector<std::pair<IntrinsicFieldHandle, std::int32_t>> fields;
            fields.reserve(kApi19OsConstants.size());
            for (const auto& value : kApi19OsConstants)
                fields.emplace_back(constants.BoundStaticField(
                    std::string(value.name), "I", kAccPublic | kAccStatic | kAccFinal), value.value);
            constants.StaticMethod("initConstants", "()V",
                [fields = std::move(fields)](IntrinsicContext& call) {
                    const IntrinsicCall c(call);
                    for (const auto& [field, value] : fields) c.SetInt(field, value);
                    return VmValue::Void();
                }, kAccPrivate | kAccStatic | kAccNative);
            catalog.push_back(std::move(constants).Build());

            auto posix = IntrinsicClassBuilder::Class(
                "Llibcore/io/Posix;", "Ljava/lang/Object;", {"Llibcore/io/Os;"},
                kAccPublic | kAccFinal);
            #include "api19_posix_natives.inc"
            posix.VirtualMethod("open", "(Ljava/lang/String;II)Ljava/io/FileDescriptor;",
                [](IntrinsicContext& call) {
                    const auto path = call.vm.StringUtf8(call.arguments[0].ref);
                    const auto flags = call.arguments[1].AsInt();
                    const auto access = flags & 3;
                    const bool readable = access == 0 || access == 2;
                    const bool writable = access == 1 || access == 2;
                    const bool create = (flags & 64) != 0;
                    const bool truncate = (flags & 512) != 0;
                    const bool append = (flags & 1024) != 0;
                    try {
                        auto file = call.vm.IO().OpenFile(path, readable, writable,
                                                          append, truncate, create);
                        const auto descriptor = call.vm.NewIntrinsicInstance(
                            "Ljava/io/FileDescriptor;");
                        call.vm.IO().SetDescriptor(descriptor, {
                            IoRuntime::DescriptorKind::vfs_path, path, 0, false,
                            {}, {}, file});
                        call.vm.IO().BindFileStream(descriptor, file, false);
                        SetIntField(call.vm, descriptor, "descriptor", file->handle);
                        return VmValue::Ref(descriptor);
                    } catch (const IoRuntimeError& error) {
                        ThrowErrno(call.vm, "open", error.ErrorNumber(), error.what());
                    }
                }, kAccPublic | kAccNative);
            posix.VirtualMethod("close", "(Ljava/io/FileDescriptor;)V",
                [](IntrinsicContext& call) {
                    const auto fd = call.arguments[0].ref;
                    static_cast<void>(PosixDescriptor(call, fd, "close"));
                    call.vm.IO().CloseDescriptor(fd);
                    SetIntField(call.vm, fd, "descriptor", -1);
                    return VmValue::Void();
                }, kAccPublic | kAccNative);
            const auto read_bytes = [](IntrinsicContext& call) {
                const auto fd = call.arguments[0].ref;
                const auto buffer = call.arguments[1].ref;
                const auto offset = call.arguments[2].AsInt();
                const auto count = call.arguments[3].AsInt();
                if (!buffer.IsValid()) throw VmJavaThrow{
                    "Ljava/lang/NullPointerException;", "buffer == null"};
                if (offset < 0 || count < 0 ||
                    static_cast<std::int64_t>(offset) + count >
                        call.vm.Model().ArrayLength(buffer))
                    throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;",
                                      "read range exceeds array"};
                try {
                    std::vector<std::byte> bytes(static_cast<std::size_t>(count));
                    const auto amount = call.vm.IO().ReadFileStream(fd, bytes);
                    if (amount == 0) return VmValue::Int(-1);
                    call.vm.Model().WriteByteRegion(
                        buffer, offset, std::span(bytes).first(amount));
                    return VmValue::Int(static_cast<std::int32_t>(amount));
                } catch (const IoRuntimeError& error) {
                    ThrowErrno(call.vm, "read", error.ErrorNumber(), error.what());
                }
            };
            posix.DirectMethod("readBytes",
                "(Ljava/io/FileDescriptor;Ljava/lang/Object;II)I", read_bytes,
                kAccPrivate | kAccNative);
            posix.DirectMethod("writeBytes",
                "(Ljava/io/FileDescriptor;Ljava/lang/Object;II)I",
                [](IntrinsicContext& call) {
                    const auto fd = call.arguments[0].ref;
                    const auto buffer = call.arguments[1].ref;
                    const auto offset = call.arguments[2].AsInt();
                    const auto count = call.arguments[3].AsInt();
                    if (!buffer.IsValid()) throw VmJavaThrow{
                        "Ljava/lang/NullPointerException;", "buffer == null"};
                    if (offset < 0 || count < 0 ||
                        static_cast<std::int64_t>(offset) + count >
                            call.vm.Model().ArrayLength(buffer))
                        throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;",
                                          "write range exceeds array"};
                    try {
                        call.vm.IO().WriteFileStream(
                            fd, call.vm.Model().ReadByteRegion(buffer, offset, count));
                        return VmValue::Int(count);
                    } catch (const IoRuntimeError& error) {
                        ThrowErrno(call.vm, "write", error.ErrorNumber(), error.what());
                    }
                }, kAccPrivate | kAccNative);
            posix.VirtualMethod("lseek", "(Ljava/io/FileDescriptor;JI)J",
                [](IntrinsicContext& call) {
                    const auto fd = call.arguments[0].ref;
                    const auto offset = call.arguments[1].AsLong();
                    const auto whence = call.arguments[2].AsInt();
                    try {
                        std::uint64_t target{};
                        if (whence == 0) {
                            if (offset < 0) throw IoRuntimeError("negative seek", 22);
                            target = static_cast<std::uint64_t>(offset);
                        } else {
                            const auto base = whence == 1
                                ? call.vm.IO().FileOffset(fd)
                                : call.vm.IO().FileSize(fd);
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
                }, kAccPublic | kAccNative);
            posix.VirtualMethod("fstat", "(Ljava/io/FileDescriptor;)Llibcore/io/StructStat;",
                [](IntrinsicContext& call) {
                    try { return VmValue::Ref(NewStructStat(
                        call, {call.vm.IO().FileSize(call.arguments[0].ref), false, true})); }
                    catch (const IoRuntimeError& error) {
                        ThrowErrno(call.vm, "fstat", error.ErrorNumber(), error.what());
                    }
                }, kAccPublic | kAccNative);
            for (const auto* name : {"stat", "lstat"}) {
                posix.VirtualMethod(name,
                    "(Ljava/lang/String;)Llibcore/io/StructStat;",
                    [name](IntrinsicContext& call) {
                        const auto path = call.vm.StringUtf8(call.arguments[0].ref);
                        const auto info = call.vm.IO().Stat(path);
                        if (!info) ThrowErrno(call.vm, name, 2, "path not found");
                        return VmValue::Ref(NewStructStat(call, *info));
                    }, kAccPublic | kAccNative);
            }
            posix.VirtualMethod("ftruncate", "(Ljava/io/FileDescriptor;J)V",
                [](IntrinsicContext& call) {
                    const auto size = call.arguments[1].AsLong();
                    if (size < 0) ThrowErrno(call.vm, "ftruncate", 22);
                    try { call.vm.IO().SetFileSize(
                        call.arguments[0].ref, static_cast<std::uint64_t>(size)); }
                    catch (const IoRuntimeError& error) {
                        ThrowErrno(call.vm, "ftruncate", error.ErrorNumber(), error.what());
                    }
                    return VmValue::Void();
                }, kAccPublic | kAccNative);
            for (const auto* name : {"fsync", "fdatasync"}) {
                posix.VirtualMethod(name, "(Ljava/io/FileDescriptor;)V",
                    [name](IntrinsicContext& call) {
                        try { call.vm.IO().SyncDescriptor(call.arguments[0].ref); }
                        catch (const IoRuntimeError& error) {
                            ThrowErrno(call.vm, name, error.ErrorNumber(), error.what());
                        }
                        return VmValue::Void();
                    }, kAccPublic | kAccNative);
            }
            posix.VirtualMethod("access", "(Ljava/lang/String;I)Z",
                [](IntrinsicContext& call) {
                    return VmValue::Int(call.vm.IO().Stat(
                        call.vm.StringUtf8(call.arguments[0].ref)).has_value());
                }, kAccPublic | kAccNative);
            posix.VirtualMethod("mkdir", "(Ljava/lang/String;I)V",
                [](IntrinsicContext& call) {
                    try {
                        call.vm.IO().MakeDirectory(
                            call.vm.StringUtf8(call.arguments[0].ref));
                    } catch (const IoRuntimeError& error) {
                        ThrowErrno(call.vm, "mkdir", error.ErrorNumber(),
                                   error.what());
                    }
                    return VmValue::Void();
                }, kAccPublic | kAccNative);
            posix.VirtualMethod("remove", "(Ljava/lang/String;)V",
                [](IntrinsicContext& call) {
                    try {
                        call.vm.IO().Delete(
                            call.vm.StringUtf8(call.arguments[0].ref));
                    } catch (const IoRuntimeError& error) {
                        ThrowErrno(call.vm, "remove", error.ErrorNumber(),
                                   error.what());
                    }
                    return VmValue::Void();
                }, kAccPublic | kAccNative);
            posix.VirtualMethod("rename", "(Ljava/lang/String;Ljava/lang/String;)V",
                [](IntrinsicContext& call) {
                    try {
                        call.vm.IO().Rename(
                            call.vm.StringUtf8(call.arguments[0].ref),
                            call.vm.StringUtf8(call.arguments[1].ref));
                    } catch (const IoRuntimeError& error) {
                        ThrowErrno(call.vm, "rename", error.ErrorNumber(),
                                   error.what());
                    }
                    return VmValue::Void();
                }, kAccPublic | kAccNative);
            posix.VirtualMethod("ioctlInt", "(Ljava/io/FileDescriptor;ILlibcore/util/MutableInt;)I",
                [](IntrinsicContext& call) {
                    try {
                        const auto available = call.vm.IO().FileAvailable(call.arguments[0].ref);
                        SetIntField(call.vm, call.arguments[2].ref, "value",
                                    static_cast<std::int32_t>(available));
                        return VmValue::Int(0);
                    } catch (const IoRuntimeError& error) {
                        ThrowErrno(call.vm, "ioctl", error.ErrorNumber(), error.what());
                    }
                }, kAccPublic | kAccNative);
            posix.VirtualMethod("strerror", "(I)Ljava/lang/String;",
                [](IntrinsicContext& call) {
                    const auto error = call.arguments[0].AsInt();
                    const char* text = error == 2 ? "No such file or directory"
                        : error == 9 ? "Bad file descriptor"
                        : error == 13 ? "Permission denied"
                        : error == 17 ? "File exists"
                        : error == 22 ? "Invalid argument" : "I/O error";
                    return VmValue::Ref(call.vm.NewStringUtf8(text));
                }, kAccPublic | kAccNative);
            posix.VirtualMethod("isatty", "(Ljava/io/FileDescriptor;)Z",
                [](IntrinsicContext&) { return VmValue::Int(0); },
                kAccPublic | kAccNative);
            posix.VirtualMethod("getaddrinfo", "(Ljava/lang/String;Llibcore/io/StructAddrinfo;)[Ljava/net/InetAddress;",
                [](IntrinsicContext& call) {
                    const auto host_ref = call.arguments[0].ref;
                    const auto host = call.vm.StringUtf8(host_ref);
                    std::vector<std::string> values;
                    const bool numeric_input = ParseAddress(host).has_value();
                    if (numeric_input) values.push_back(host);
                    else {
                        try { values = call.vm.Network().Resolve(host); }
                        catch (const NetworkRuntimeError& error) { ThrowGai(call.vm, "getaddrinfo", error.what()); }
                    }
                    const auto array = call.vm.Model().NewObjectArray(
                        call.vm.Linker().ResolveDescriptor("[Ljava/net/InetAddress;"),
                        call.vm.Linker().ResolveDescriptor("Ljava/net/InetAddress;"),
                        static_cast<JniSize>(values.size()));
                    const auto roots = call.vm.ProtectReferences(std::array{array, host_ref});
                    for (std::size_t i = 0; i < values.size(); ++i) {
                        const auto bytes = ParseAddress(values[i]);
                        if (!bytes) ThrowGai(call.vm, "getaddrinfo", "transport returned a non-numeric address");
                        call.vm.Model().SetObjectElement(array, static_cast<JniSize>(i),
                            NewAddress(call.vm, *bytes, numeric_input ? VmObjectRef{0} : host_ref));
                    }
                    return VmValue::Ref(array);
                }, kAccPublic | kAccNative);
            posix.VirtualMethod("inet_pton", "(ILjava/lang/String;)Ljava/net/InetAddress;",
                [](IntrinsicContext& call) {
                    const auto parsed = ParseAddress(call.vm.StringUtf8(call.arguments[1].ref));
                    return VmValue::Ref(parsed ? NewAddress(call.vm, *parsed) : VmObjectRef{0});
                }, kAccPublic | kAccNative);
            posix.VirtualMethod("getnameinfo", "(Ljava/net/InetAddress;I)Ljava/lang/String;",
                [](IntrinsicContext& call) {
                    const auto numeric = NumericAddress(call.vm, call.arguments[0].ref);
                    if ((call.arguments[1].AsInt() & 2) != 0)
                        return VmValue::Ref(call.vm.NewStringUtf8(numeric));
                    try { return VmValue::Ref(call.vm.NewStringUtf8(call.vm.Network().Reverse(numeric))); }
                    catch (const NetworkRuntimeError& error) { ThrowGai(call.vm, "getnameinfo", error.what()); }
                }, kAccPublic | kAccNative);
            posix.VirtualMethod("uname", "()Llibcore/io/StructUtsname;",
                [](IntrinsicContext& call) {
                    const auto& identity = call.vm.Network().Policy().host_identity;
                    if (!identity) throw VmJavaThrow{"Ljava/lang/UnsupportedOperationException;",
                                                     "guest host identity is not configured"};
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
                    InvokeDirect(call.vm, "Llibcore/io/StructUtsname;", "<init>",
                        "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V",
                        {VmValue::Ref(result), VmValue::Ref(sysname), VmValue::Ref(nodename),
                         VmValue::Ref(release), VmValue::Ref(version), VmValue::Ref(machine)});
                    return VmValue::Ref(result);
                }, kAccPublic | kAccNative);
            posix.VirtualMethod("gai_strerror", "(I)Ljava/lang/String;",
                [](IntrinsicContext& call) { return VmValue::Ref(call.vm.NewStringUtf8("name or service not known")); },
                kAccPublic | kAccNative);
            catalog.push_back(std::move(posix).Build());
        }

        NetworkRuntime::Endpoint EndpointFrom(IntrinsicContext& call,
                                              const VmObjectRef address,
                                              const std::int32_t port);
        NetworkRuntime::Endpoint HostEndpoint(IntrinsicContext& call,
                                              VmObjectRef host_ref,
                                              std::int32_t port);

        NetworkRuntime::Endpoint SocketEndpoint(IntrinsicContext& call, VmObjectRef socket_address) {
            if (!socket_address.IsValid())
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "endpoint is null"};
            if (!call.vm.Linker().IsAssignable(call.vm.Linker().ResolveDescriptor("Ljava/net/InetSocketAddress;"),
                                               call.vm.Model().ObjectClass(socket_address)))
                throw VmJavaThrow{"Ljava/lang/IllegalArgumentException;", "unsupported SocketAddress"};
            const auto address = detail::InvokeGuest(call.vm, socket_address, "getAddress", "()Ljava/net/InetAddress;").ref;
            const auto port = detail::InvokeGuest(call.vm, socket_address, "getPort", "()I").AsInt();
            if (address.IsValid())
                return EndpointFrom(call, address, port);
            const auto host = detail::InvokeGuest(call.vm, socket_address, "getHostName",
                                                  "()Ljava/lang/String;").ref;
            if (!host.IsValid())
                throw VmJavaThrow{"Ljava/net/UnknownHostException;", "unresolved socket endpoint"};
            return HostEndpoint(call, host, port);
        }

        NetworkRuntime::Endpoint HostEndpoint(IntrinsicContext& call,
                                              VmObjectRef host_ref,
                                              std::int32_t port);

        [[noreturn]] void ThrowNetwork(const NetworkRuntimeError& error) {
            throw VmJavaThrow{"Ljava/net/SocketException;", error.what()};
        }

        NetworkRuntime::Endpoint EndpointFrom(IntrinsicContext& call,
                                              const VmObjectRef address,
                                              const std::int32_t port) {
            if (port < 0 || port > 65535)
                throw VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;",
                    "port out of range"
                };
            if (!address.IsValid())
                throw VmJavaThrow{
                    "Ljava/lang/NullPointerException;",
                    "address is null"
                };
            try {
                const auto numeric = detail::InvokeGuest(call.vm, address, "getHostAddress", "()Ljava/lang/String;").ref;
                const auto numeric_root = call.vm.ProtectReferences(std::array{numeric});
                const auto host = detail::InvokeGuest(call.vm, address, "getHostName", "()Ljava/lang/String;").ref;
                NetworkRuntime::Endpoint endpoint{call.vm.StringUtf8(host), call.vm.StringUtf8(numeric), 0};
                endpoint.port = static_cast<std::uint16_t>(port);
                return endpoint;
            } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
        }

        NetworkRuntime::Endpoint HostEndpoint(IntrinsicContext& call,
                                              const VmObjectRef host_ref,
                                              const std::int32_t port) {
            if (!host_ref.IsValid())
                throw VmJavaThrow{"Ljava/lang/NullPointerException;", "host is null"};
            if (port < 0 || port > 65535)
                throw VmJavaThrow{
                    "Ljava/lang/IllegalArgumentException;",
                    "port out of range"
                };
            const auto host = call.vm.StringUtf8(host_ref);
            try {
                const auto addresses = call.vm.Network().Resolve(host);
                return {host, addresses.front(), static_cast<std::uint16_t>(port)};
            } catch (const NetworkRuntimeError& error) {
                throw VmJavaThrow{"Ljava/net/UnknownHostException;", error.what()};
            }
        }

        IntrinsicClassDecl DeclareSocketInputStream() {
            auto builder = IntrinsicClassBuilder::Class(
                "Ljava/net/SocketInputStream;", "Ljava/io/InputStream;");
            builder.FinalOverrideMethod("read", "([BII)I", [](IntrinsicContext& call) {
                const auto array = call.arguments[0].ref;
                const auto offset = call.arguments[1].AsInt();
                const auto length = call.arguments[2].AsInt();
                detail::CheckRegion(call.vm.Model().ArrayLength(array), offset, length);
                const auto depth = call.vm.ExecutionLock().ReleaseForBlocking();
                try {
                    const auto bytes = call.vm.Network().ReadStream(
                        call.receiver, static_cast<std::size_t>(length));
                    call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
                    if (bytes.empty()) return VmValue::Int(-1);
                    call.vm.Model().WriteByteRegion(array, offset, bytes);
                    return VmValue::Int(static_cast<std::int32_t>(bytes.size()));
                } catch (const NetworkRuntimeError& error) {
                    call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
                    ThrowNetwork(error);
                } catch (...) {
                    call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
                    throw;
                }
            });
            builder.FinalOverrideMethod("read", "()I", [](IntrinsicContext& call) {
                const auto depth = call.vm.ExecutionLock().ReleaseForBlocking();
                try {
                    const auto bytes = call.vm.Network().ReadStream(call.receiver, 1);
                    call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
                    return VmValue::Int(bytes.empty() ? -1 : static_cast<std::uint8_t>(bytes.front()));
                } catch (const NetworkRuntimeError& error) {
                    call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
                    ThrowNetwork(error);
                } catch (...) {
                    call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
                    throw;
                }
            });
            return std::move(builder).Build();
        }

        IntrinsicClassDecl DeclareSocketOutputStream() {
            auto builder = IntrinsicClassBuilder::Class(
                "Ljava/net/SocketOutputStream;", "Ljava/io/OutputStream;");
            builder.FinalOverrideMethod("write", "([BII)V", [](IntrinsicContext& call) {
                const auto array = call.arguments[0].ref;
                const auto offset = call.arguments[1].AsInt();
                const auto length = call.arguments[2].AsInt();
                detail::CheckRegion(call.vm.Model().ArrayLength(array), offset, length);
                const auto depth = call.vm.ExecutionLock().ReleaseForBlocking();
                try {
                    call.vm.Network().WriteStream(call.receiver,
                                                  call.vm.Model().ReadByteRegion(array, offset, length));
                    call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
                    return VmValue::Void();
                } catch (const NetworkRuntimeError& error) {
                    call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
                    ThrowNetwork(error);
                } catch (...) {
                    call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
                    throw;
                }
            });
            builder.FinalOverrideMethod("write", "(I)V", [](IntrinsicContext& call) {
                const auto byte = static_cast<std::byte>(call.arguments[0].AsInt());
                const auto depth = call.vm.ExecutionLock().ReleaseForBlocking();
                try {
                    call.vm.Network().WriteStream(call.receiver,
                                                  std::span(&byte, 1));
                    call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
                    return VmValue::Void();
                } catch (const NetworkRuntimeError& error) {
                    call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
                    ThrowNetwork(error);
                } catch (...) {
                    call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
                    throw;
                }
            });
            builder.FinalOverrideMethod("flush", "()V", NoopVoid());
            return std::move(builder).Build();
        }

        IntrinsicClassDecl DeclareSocket() {
            auto builder = IntrinsicClassBuilder::Class(
                "Ljava/net/Socket;", "Ljava/lang/Object;");
            const auto connect_owner = [](IntrinsicContext& call, NetworkRuntime::Endpoint endpoint) {
                const auto depth = call.vm.ExecutionLock().ReleaseForBlocking();
                try {
                    call.vm.Network().Connect(call.receiver, std::move(endpoint));
                    call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
                } catch (const NetworkRuntimeError& error) {
                    call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
                    ThrowNetwork(error);
                } catch (...) {
                    call.vm.ExecutionLock().ReacquireAfterBlocking(depth);
                    throw;
                }
            };
            builder.Constructor("()V", [](IntrinsicContext& call) {
                call.vm.Network().CreateSocket(call.receiver, false);
                return VmValue::Void();
            });
            builder.Constructor("(Ljava/lang/String;I)V", [connect_owner](IntrinsicContext& call) {
                call.vm.Network().CreateSocket(call.receiver, false);
                connect_owner(call, HostEndpoint(call, call.arguments[0].ref,
                                                 call.arguments[1].AsInt()));
                return VmValue::Void();
            });
            builder.Constructor("(Ljava/net/InetAddress;I)V", [connect_owner](IntrinsicContext& call) {
                call.vm.Network().CreateSocket(call.receiver, false);
                connect_owner(call, EndpointFrom(call, call.arguments[0].ref,
                                                 call.arguments[1].AsInt()));
                return VmValue::Void();
            });
            builder.VirtualMethod("connect", "(Ljava/net/SocketAddress;)V",
                                  [connect_owner](IntrinsicContext& call) {
                connect_owner(call, SocketEndpoint(call, call.arguments[0].ref));
                return VmValue::Void();
            });
            builder.VirtualMethod("connect", "(Ljava/net/SocketAddress;I)V",
                                  [connect_owner](IntrinsicContext& call) {
                try {
                    call.vm.Network().SetTimeout(call.receiver, call.arguments[1].AsInt());
                } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
                connect_owner(call, SocketEndpoint(call, call.arguments[0].ref));
                return VmValue::Void();
            });
            builder.VirtualMethod("getInputStream", "()Ljava/io/InputStream;", [](IntrinsicContext& call) {
                const auto stream = call.vm.NewIntrinsicInstance(
                    "Ljava/net/SocketInputStream;");
                try { call.vm.Network().BindStream(stream, call.receiver, false); } catch (const
                    NetworkRuntimeError& error) { ThrowNetwork(error); }
                return VmValue::Ref(stream);
            });
            builder.VirtualMethod("getOutputStream", "()Ljava/io/OutputStream;", [](IntrinsicContext& call) {
                const auto stream = call.vm.NewIntrinsicInstance(
                    "Ljava/net/SocketOutputStream;");
                try { call.vm.Network().BindStream(stream, call.receiver, true); } catch (const
                    NetworkRuntimeError& error) { ThrowNetwork(error); }
                return VmValue::Ref(stream);
            });
            builder.VirtualMethod("isConnected", "()Z", [](IntrinsicContext& call) {
                try {
                    return VmValue::Int(call.vm.Network().GetSocket(
                                            call.receiver).connected
                                            ? 1
                                            : 0);
                } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
            });
            builder.VirtualMethod("isClosed", "()Z", [](IntrinsicContext& call) {
                try {
                    return VmValue::Int(call.vm.Network().GetSocket(
                                            call.receiver).closed
                                            ? 1
                                            : 0);
                } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
            });
            builder.VirtualMethod("close", "()V", [](IntrinsicContext& call) {
                call.vm.Network().CloseSocket(call.receiver);
                return VmValue::Void();
            });
            builder.VirtualMethod("getPort", "()I", [](IntrinsicContext& call) {
                try {
                    return VmValue::Int(call.vm.Network().GetSocket(call.receiver).endpoint.port);
                } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
            });
            builder.VirtualMethod("setSoTimeout", "(I)V", [](IntrinsicContext& call) {
                try {
                    call.vm.Network().SetTimeout(call.receiver, call.arguments[0].AsInt());
                } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
                return VmValue::Void();
            });
            builder.VirtualMethod("getSoTimeout", "()I", [](IntrinsicContext& call) {
                try {
                    return VmValue::Int(call.vm.Network().GetSocket(call.receiver).timeout_ms);
                } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
            });
            return std::move(builder).Build();
        }

        IntrinsicClassDecl DeclareDatagramPacket() {
            auto builder = IntrinsicClassBuilder::Class(
                "Ljava/net/DatagramPacket;", "Ljava/lang/Object;");
            builder.Constructor("([BII)V", [](IntrinsicContext& call) {
                detail::CheckRegion(call.vm.Model().ArrayLength(call.arguments[0].ref),
                                    call.arguments[1].AsInt(),
                                    call.arguments[2].AsInt());
                call.vm.Network().SetPacket(call.receiver,
                                            {
                                                call.arguments[0].ref, call.arguments[1].AsInt(),
                                                call.arguments[2].AsInt(), {}
                                            });
                return VmValue::Void();
            });
            builder.Constructor("([BIILjava/net/InetAddress;I)V", [](IntrinsicContext& call) {
                detail::CheckRegion(call.vm.Model().ArrayLength(call.arguments[0].ref),
                                    call.arguments[1].AsInt(),
                                    call.arguments[2].AsInt());
                call.vm.Network().SetPacket(call.receiver,
                                            {
                                                call.arguments[0].ref, call.arguments[1].AsInt(),
                                                call.arguments[2].AsInt(), EndpointFrom(
                                                    call, call.arguments[3].ref,
                                                    call.arguments[4].AsInt())
                                            });
                return VmValue::Void();
            });
            builder.FinalMethod("getLength", "()I", [](IntrinsicContext& call) {
                try { return VmValue::Int(call.vm.Network().Packet(call.receiver).length); } catch (const
                    NetworkRuntimeError& error) { ThrowNetwork(error); }
            });
            builder.FinalMethod("getPort", "()I", [](IntrinsicContext& call) {
                try {
                    return VmValue::Int(call.vm.Network().Packet(
                        call.receiver).endpoint.port);
                } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
            });
            return std::move(builder).Build();
        }

        IntrinsicClassDecl DeclareDatagramSocket() {
            auto builder = IntrinsicClassBuilder::Class(
                "Ljava/net/DatagramSocket;", "Ljava/lang/Object;");
            builder.Constructor("()V", NoopVoid());
            builder.FinalMethod("send", "(Ljava/net/DatagramPacket;)V", [](IntrinsicContext& call) {
                try {
                    const auto& packet = call.vm.Network().Packet(call.arguments[0].ref);
                    call.vm.Network().SendPacket(call.arguments[0].ref,
                                                 call.vm.Model().ReadByteRegion(
                                                     packet.array, packet.offset,
                                                     packet.length));
                    return VmValue::Void();
                } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
            });
            builder.FinalMethod("receive", "(Ljava/net/DatagramPacket;)V", [](IntrinsicContext& call) {
                try {
                    auto& packet = call.vm.Network().Packet(call.arguments[0].ref);
                    const auto datagram = call.vm.Network().ReceivePacket(packet.length);
                    const auto amount = std::min<std::size_t>(
                        datagram.payload.size(), packet.length);
                    call.vm.Model().WriteByteRegion(packet.array, packet.offset,
                                                    std::span(datagram.payload).first(amount));
                    packet.length = static_cast<std::int32_t>(amount);
                    packet.endpoint = {datagram.host, {}, datagram.port};
                    return VmValue::Void();
                } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
            });
            builder.FinalMethod("close", "()V", NoopVoid());
            return std::move(builder).Build();
        }

        IntrinsicClassDecl DeclareSocketFactory(const CoreIntrinsicServices& services) {
            auto builder = IntrinsicClassBuilder::Class(
                "Ljavax/net/SocketFactory;", "Ljava/lang/Object;");
            builder.StaticMethod("getDefault", "()Ljavax/net/SocketFactory;", [services](IntrinsicContext& call) {
                if (services.singleton) {
                    return VmValue::Ref(services.singleton(
                        call.vm, "socket_factory", "Ljavax/net/SocketFactory;"));
                }
                return VmValue::Ref(call.vm.NewIntrinsicInstance(
                    "Ljavax/net/SocketFactory;"));
            });
            builder.VirtualMethod("createSocket", "()Ljava/net/Socket;", [](IntrinsicContext& call) {
                const auto socket = call.vm.NewIntrinsicInstance("Ljava/net/Socket;");
                call.vm.Network().CreateSocket(socket, false);
                return VmValue::Ref(socket);
            });
            builder.VirtualMethod("createSocket", "(Ljava/lang/String;I)Ljava/net/Socket;", [](IntrinsicContext& call) {
                const auto socket = call.vm.NewIntrinsicInstance("Ljava/net/Socket;");
                call.vm.Network().CreateSocket(socket, false);
                try {
                    call.vm.Network().Connect(socket,
                                              HostEndpoint(call, call.arguments[0].ref,
                                                           call.arguments[1].AsInt()));
                } catch (const NetworkRuntimeError& error) { ThrowNetwork(error); }
                return VmValue::Ref(socket);
            });
            return std::move(builder).Build();
        }

        using namespace detail;

        IntrinsicClassDecl DeclareMalformedURLException() {
            return DeclareSimpleThrowable("Ljava/net/MalformedURLException;", "Ljava/io/IOException;");
        }
    } // namespace

    void AppendJavaNetPlatform(std::vector<IntrinsicClassDecl>& catalog,
                               const CoreIntrinsicServices& services) {
        catalog.push_back(DeclarePlatformHttpURLConnection());
        catalog.push_back(DeclareProxySelector());
        catalog.push_back(DeclarePlatformUrl());
        catalog.push_back(DeclarePlatformUrlConnection());
        catalog.push_back(DeclarePlatformUrlEncoder());
        catalog.push_back(DeclarePlatformUrlDecoder());
        AppendAddressNatives(catalog);
        catalog.push_back(DeclareSocketInputStream());
        catalog.push_back(DeclareSocketOutputStream());
        catalog.push_back(DeclareSocket());
        catalog.push_back(DeclareDatagramPacket());
        catalog.push_back(DeclareDatagramSocket());
        catalog.push_back(DeclareSocketFactory(services));

        catalog.push_back(DeclareMalformedURLException());
    }
} // namespace ogplay::runtime::dexvm::intrinsics
