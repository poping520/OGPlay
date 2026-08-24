#include "catalog.h"
#include "shared.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/io_runtime.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

[[noreturn]] void IoFailure(const IoRuntimeError &error) {
  throw VmJavaThrow{"Ljava/io/IOException;", error.what()};
}

[[nodiscard]] IoRuntime::InputState &Input(IntrinsicContext &call) {
  try {
    return call.vm.IO().Input(call.receiver);
  } catch (const IoRuntimeError &error) {
    IoFailure(error);
  }
}

[[nodiscard]] IoRuntime::OutputState &Output(IntrinsicContext &call) {
  try {
    return call.vm.IO().Output(call.receiver);
  } catch (const IoRuntimeError &error) {
    IoFailure(error);
  }
}

IntrinsicHandler AdoptInput() {
  return [](IntrinsicContext &call) {
    try {
      call.vm.IO().AdoptInput(call.arguments[0].ref, call.receiver);
    } catch (const IoRuntimeError &error) {
      IoFailure(error);
    }
    return VmValue::Void();
  };
}

IntrinsicHandler AdoptOutput() {
  return [](IntrinsicContext &call) {
    try {
      call.vm.IO().AdoptOutput(call.arguments[0].ref, call.receiver);
    } catch (const IoRuntimeError &error) {
      IoFailure(error);
    }
    return VmValue::Void();
  };
}

IntrinsicHandler WriteRange() {
  return [](IntrinsicContext &call) {
    auto &output = Output(call);
    const auto array = call.arguments[0].ref;
    const auto offset = call.arguments[1].AsInt();
    const auto length = call.arguments[2].AsInt();
    if (offset < 0 || length < 0 ||
        static_cast<std::int64_t>(offset) + length >
            call.vm.Model().ArrayLength(array)) {
      throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;",
                        "write range exceeds the source array"};
    }
    const auto bytes = call.vm.Model().ReadByteRegion(array, offset, length);
    output.bytes.insert(output.bytes.end(), bytes.begin(), bytes.end());
    return VmValue::Void();
  };
}

IntrinsicHandler WriteBytes() {
  return [](IntrinsicContext &call) {
    auto &output = Output(call);
    const auto array = call.arguments[0].ref;
    const auto bytes = call.vm.Model().ReadByteRegion(
        array, 0, call.vm.Model().ArrayLength(array));
    output.bytes.insert(output.bytes.end(), bytes.begin(), bytes.end());
    return VmValue::Void();
  };
}

IntrinsicHandler FlushOutput(const bool close) {
  return [close](IntrinsicContext &call) {
    try {
      call.vm.IO().FlushOutput(call.receiver, close);
    } catch (const IoRuntimeError &error) {
      IoFailure(error);
    }
    return VmValue::Void();
  };
}

IntrinsicClassDecl DeclareInputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/InputStream;",
                                              "Ljava/lang/Object;");
  builder.VirtualMethod("read", "([BII)I", [](IntrinsicContext &call) {
    auto &input = Input(call);
    const auto offset = call.arguments[1].AsInt();
    const auto length = call.arguments[2].AsInt();
    const auto array = call.arguments[0].ref;
    if (offset < 0 || length < 0 ||
        static_cast<std::int64_t>(offset) + length >
            call.vm.Model().ArrayLength(array)) {
      throw VmJavaThrow{"Ljava/lang/IndexOutOfBoundsException;",
                        "invalid stream read range"};
    }
    if (length == 0)
      return VmValue::Int(0);
    const auto remaining = input.bytes.size() - input.cursor;
    if (remaining == 0)
      return VmValue::Int(-1);
    const auto amount = std::min<std::size_t>(length, remaining);
    call.vm.Model().WriteByteRegion(
        array, offset, std::span(input.bytes).subspan(input.cursor, amount));
    input.cursor += amount;
    return VmValue::Int(static_cast<std::int32_t>(amount));
  });
  builder.VirtualMethod("read", "([B)I", [](IntrinsicContext &call) {
    auto &input = Input(call);
    const auto array = call.arguments[0].ref;
    const auto capacity = call.vm.Model().ArrayLength(array);
    if (capacity == 0)
      return VmValue::Int(0);
    const auto remaining = input.bytes.size() - input.cursor;
    if (remaining == 0)
      return VmValue::Int(-1);
    const auto amount = std::min<std::size_t>(capacity, remaining);
    call.vm.Model().WriteByteRegion(
        array, 0, std::span(input.bytes).subspan(input.cursor, amount));
    input.cursor += amount;
    return VmValue::Int(static_cast<std::int32_t>(amount));
  });
  builder.VirtualMethod("read", "()I", [](IntrinsicContext &call) {
    auto &input = Input(call);
    if (input.cursor >= input.bytes.size())
      return VmValue::Int(-1);
    return VmValue::Int(static_cast<std::uint8_t>(input.bytes[input.cursor++]));
  });
  builder.VirtualMethod("available", "()I", [](IntrinsicContext &call) {
    const auto &input = Input(call);
    return VmValue::Int(
        static_cast<std::int32_t>(input.bytes.size() - input.cursor));
  });
  builder.VirtualMethod("skip", "(J)J", [](IntrinsicContext &call) {
    auto &input = Input(call);
    const auto remaining =
        static_cast<std::int64_t>(input.bytes.size() - input.cursor);
    const auto amount = std::max<std::int64_t>(
        0, std::min(call.arguments[0].AsLong(), remaining));
    input.cursor += static_cast<std::size_t>(amount);
    return VmValue::Long(amount);
  });
  builder.VirtualMethod("close", "()V", [](IntrinsicContext &call) {
    call.vm.IO().CloseInput(call.receiver);
    return VmValue::Void();
  });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareOutputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/OutputStream;",
                                              "Ljava/lang/Object;");
  builder.VirtualMethod("write", "([BII)V", WriteRange());
  builder.VirtualMethod("write", "([B)V", WriteBytes());
  builder.VirtualMethod("write", "(I)V", [](IntrinsicContext &call) {
    Output(call).bytes.push_back(
        static_cast<std::byte>(call.arguments[0].AsInt() & 0xff));
    return VmValue::Void();
  });
  builder.VirtualMethod("flush", "()V", FlushOutput(false));
  builder.VirtualMethod("close", "()V", FlushOutput(true));
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareByteArrayInputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/ByteArrayInputStream;",
                                              "Ljava/io/InputStream;");
  builder.Constructor("([B)V", [](IntrinsicContext &call) {
    const auto array = call.arguments[0].ref;
    call.vm.IO().SetInput(call.receiver,
                          {call.vm.Model().ReadByteRegion(
                               array, 0, call.vm.Model().ArrayLength(array)),
                           0, false});
    return VmValue::Void();
  });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareByteArrayOutputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/ByteArrayOutputStream;",
                                              "Ljava/io/OutputStream;");
  builder.Constructor("()V", [](IntrinsicContext &call) {
    call.vm.IO().SetOutput(call.receiver, {{}, {}, false});
    return VmValue::Void();
  });
  builder.FinalMethod("write", "([BII)V", WriteRange());
  builder.FinalMethod("write", "([B)V", WriteBytes());
  builder.FinalMethod("toByteArray", "()[B", [](IntrinsicContext &call) {
    const auto &bytes = Output(call).bytes;
    const auto array_class = call.vm.Linker().ResolveDescriptor("[B");
    const auto array =
        call.vm.Model().NewPrimitiveArray(array_class, JniPrimitiveKind::byte,
                                          static_cast<JniSize>(bytes.size()));
    if (!bytes.empty())
      call.vm.Model().WriteByteRegion(array, 0, bytes);
    return VmValue::Ref(array);
  });
  builder.FinalMethod("size", "()I", [](IntrinsicContext &call) {
    return VmValue::Int(static_cast<std::int32_t>(Output(call).bytes.size()));
  });
  builder.FinalMethod(
      "toString", "()Ljava/lang/String;", [](IntrinsicContext &call) {
        const auto &bytes = Output(call).bytes;
        return VmValue::Ref(call.vm.NewStringUtf8(std::string(
            reinterpret_cast<const char *>(bytes.data()), bytes.size())));
      });
  builder.FinalMethod("close", "()V",
                      [](IntrinsicContext &) { return VmValue::Void(); });
  return std::move(builder).Build();
}

IntrinsicClassDecl BuildWrapper(std::string descriptor, std::string superclass,
                                std::string argument, const bool output,
                                const bool capacity = false) {
  auto builder = IntrinsicClassBuilder::Class(std::move(descriptor),
                                              std::move(superclass));
  const auto handler = output ? AdoptOutput() : AdoptInput();
  builder.Constructor("(" + argument + ")V", handler);
  if (capacity)
    builder.Constructor("(" + argument + "I)V", handler);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareReader() {
  return std::move(IntrinsicClassBuilder::Class("Ljava/io/Reader;",
                                                "Ljava/lang/Object;"))
      .Build();
}

IntrinsicClassDecl DeclareWriter() {
  return std::move(IntrinsicClassBuilder::Class("Ljava/io/Writer;",
                                                "Ljava/lang/Object;"))
      .Build();
}

IntrinsicClassDecl DeclareFilterInputStream() {
  return BuildWrapper("Ljava/io/FilterInputStream;", "Ljava/io/InputStream;",
                      "Ljava/io/InputStream;", false);
}

IntrinsicClassDecl DeclareFilterOutputStream() {
  return BuildWrapper("Ljava/io/FilterOutputStream;", "Ljava/io/OutputStream;",
                      "Ljava/io/OutputStream;", true);
}

IntrinsicClassDecl DeclareBufferedInputStream() {
  return BuildWrapper("Ljava/io/BufferedInputStream;",
                      "Ljava/io/FilterInputStream;", "Ljava/io/InputStream;",
                      false, true);
}

IntrinsicClassDecl DeclareBufferedOutputStream() {
  return BuildWrapper("Ljava/io/BufferedOutputStream;",
                      "Ljava/io/FilterOutputStream;", "Ljava/io/OutputStream;",
                      true, true);
}

IntrinsicClassDecl DeclareInputStreamReader() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/InputStreamReader;",
                                              "Ljava/io/Reader;");
  const auto handler = AdoptInput();
  builder.Constructor("(Ljava/io/InputStream;)V", handler);
  builder.Constructor("(Ljava/io/InputStream;Ljava/nio/charset/Charset;)V",
                      handler);
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareBufferedReader() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/BufferedReader;",
                                              "Ljava/io/Reader;");
  builder.Constructor("(Ljava/io/Reader;)V", AdoptInput());
  builder.FinalMethod(
      "readLine", "()Ljava/lang/String;", [](IntrinsicContext &call) {
        auto &input = Input(call);
        if (input.cursor >= input.bytes.size()) {
          return VmValue::Ref(VmObjectRef{});
        }
        std::string line;
        while (input.cursor < input.bytes.size()) {
          const auto byte = static_cast<char>(input.bytes[input.cursor++]);
          if (byte == '\n')
            break;
          if (byte == '\r') {
            if (input.cursor < input.bytes.size() &&
                static_cast<char>(input.bytes[input.cursor]) == '\n') {
              ++input.cursor;
            }
            break;
          }
          line.push_back(byte);
        }
        return VmValue::Ref(call.vm.NewStringUtf8(line));
      });
  builder.FinalMethod("ready", "()Z", [](IntrinsicContext &call) {
    const auto &input = Input(call);
    return VmValue::Int(input.cursor < input.bytes.size());
  });
  builder.FinalMethod("close", "()V", [](IntrinsicContext &call) {
    call.vm.IO().CloseInput(call.receiver);
    return VmValue::Void();
  });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareDataInputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/DataInputStream;",
                                              "Ljava/io/InputStream;");
  builder.Constructor("(Ljava/io/InputStream;)V", AdoptInput());
  const auto take = [](IntrinsicContext &call, const std::size_t count) {
    auto &input = Input(call);
    if (input.bytes.size() - input.cursor < count) {
      throw VmJavaThrow{"Ljava/io/EOFException;", "end of stream"};
    }
    const auto begin = input.cursor;
    input.cursor += count;
    return std::span(input.bytes).subspan(begin, count);
  };
  builder.FinalMethod("readFully", "([B)V", [take](IntrinsicContext &call) {
    const auto array = call.arguments[0].ref;
    const auto count =
        static_cast<std::size_t>(call.vm.Model().ArrayLength(array));
    call.vm.Model().WriteByteRegion(array, 0, take(call, count));
    return VmValue::Void();
  });
  builder.FinalMethod("skipBytes", "(I)I", [](IntrinsicContext &call) {
    auto &input = Input(call);
    const auto requested = call.arguments[0].AsInt();
    const auto amount = std::min<std::size_t>(
        requested > 0 ? static_cast<std::size_t>(requested) : 0,
        input.bytes.size() - input.cursor);
    input.cursor += amount;
    return VmValue::Int(static_cast<std::int32_t>(amount));
  });
  builder.FinalMethod("readInt", "()I", [take](IntrinsicContext &call) {
    std::uint32_t value{};
    for (const auto byte : take(call, 4)) {
      value = (value << 8U) | static_cast<std::uint8_t>(byte);
    }
    return VmValue::Int(static_cast<std::int32_t>(value));
  });
  builder.FinalMethod("readLong", "()J", [take](IntrinsicContext &call) {
    std::uint64_t value{};
    for (const auto byte : take(call, 8)) {
      value = (value << 8U) | static_cast<std::uint8_t>(byte);
    }
    return VmValue::Long(static_cast<std::int64_t>(value));
  });
  builder.FinalMethod(
      "readUTF", "()Ljava/lang/String;", [take](IntrinsicContext &call) {
        const auto length_bytes = take(call, 2);
        const auto length = static_cast<std::size_t>(
            (static_cast<std::uint8_t>(length_bytes[0]) << 8U) |
            static_cast<std::uint8_t>(length_bytes[1]));
        const auto bytes = take(call, length);
        return VmValue::Ref(call.vm.NewStringUtf8(std::string(
            reinterpret_cast<const char *>(bytes.data()), bytes.size())));
      });
  builder.FinalMethod("close", "()V", [](IntrinsicContext &call) {
    call.vm.IO().CloseInput(call.receiver);
    return VmValue::Void();
  });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareDataOutputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/DataOutputStream;",
                                              "Ljava/io/OutputStream;");
  builder.Constructor("(Ljava/io/OutputStream;)V", AdoptOutput());
  builder.FinalMethod(
      "writeUTF", "(Ljava/lang/String;)V", [](IntrinsicContext &call) {
        auto &bytes = Output(call).bytes;
        const auto text = call.vm.StringUtf8(call.arguments[0].ref);
        bytes.push_back(static_cast<std::byte>((text.size() >> 8U) & 0xffU));
        bytes.push_back(static_cast<std::byte>(text.size() & 0xffU));
        for (const auto character : text) {
          bytes.push_back(static_cast<std::byte>(character));
        }
        return VmValue::Void();
      });
  builder.FinalMethod("close", "()V", FlushOutput(true));
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareObjectInputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/ObjectInputStream;",
                                              "Ljava/io/InputStream;");
  builder.Constructor("()V",
                      [](IntrinsicContext &) { return VmValue::Void(); });
  return std::move(builder).Build();
}

} // namespace

void AppendJavaIoStreams(std::vector<IntrinsicClassDecl> &catalog) {
  catalog.push_back(DeclareInputStream());
  catalog.push_back(DeclareOutputStream());
  catalog.push_back(DeclareReader());
  catalog.push_back(DeclareWriter());
  catalog.push_back(DeclareByteArrayInputStream());
  catalog.push_back(DeclareByteArrayOutputStream());
  catalog.push_back(DeclareFilterInputStream());
  catalog.push_back(DeclareFilterOutputStream());
  catalog.push_back(DeclareBufferedInputStream());
  catalog.push_back(DeclareBufferedOutputStream());
  catalog.push_back(DeclareBufferedReader());
  catalog.push_back(DeclareInputStreamReader());
  catalog.push_back(DeclareDataInputStream());
  catalog.push_back(DeclareDataOutputStream());
  catalog.push_back(DeclareObjectInputStream());
}

} // namespace ogplay::runtime::dexvm::intrinsics
