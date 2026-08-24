#include "catalog.h"
#include "shared.h"

#include <cstddef>
#include <string>
#include <utility>

#include "ogplay/runtime/dexvm/intrinsic_builder.h"
#include "ogplay/runtime/dexvm/io_runtime.h"

namespace ogplay::runtime::dexvm::intrinsics {
namespace {

[[nodiscard]] std::string FilePath(IntrinsicContext &call,
                                   const VmObjectRef file) {
  const auto slots = call.vm.Model().InstanceSlots(file);
  return call.vm.StringUtf8(VmObjectRef(slots[0].bits));
}

[[noreturn]] void IoFailure(const IoRuntimeError &error) {
  throw VmJavaThrow{"Ljava/io/IOException;", error.what()};
}

IntrinsicHandler OpenInputFromPath(const bool file_argument) {
  return [file_argument](IntrinsicContext &call) {
    const auto path = file_argument ? FilePath(call, call.arguments[0].ref)
                                    : call.vm.StringUtf8(call.arguments[0].ref);
    const auto bytes = call.vm.IO().ReadFile(path);
    if (!bytes.has_value()) {
      throw VmJavaThrow{"Ljava/io/FileNotFoundException;",
                        "file not found: " + path};
    }
    call.vm.IO().SetInput(call.receiver, {*bytes, 0, false});
    return VmValue::Void();
  };
}

IntrinsicHandler OpenOutputFromPath(const bool file_argument) {
  return [file_argument](IntrinsicContext &call) {
    const auto path = file_argument ? FilePath(call, call.arguments[0].ref)
                                    : call.vm.StringUtf8(call.arguments[0].ref);
    call.vm.IO().SetOutput(call.receiver, {path, {}, false});
    return VmValue::Void();
  };
}

IntrinsicHandler WriteBytes() {
  return [](IntrinsicContext &call) {
    IoRuntime::OutputState *output{};
    try {
      output = &call.vm.IO().Output(call.receiver);
    } catch (const IoRuntimeError &error) {
      IoFailure(error);
    }
    const auto array = call.arguments[0].ref;
    const auto bytes = call.vm.Model().ReadByteRegion(
        array, 0, call.vm.Model().ArrayLength(array));
    output->bytes.insert(output->bytes.end(), bytes.begin(), bytes.end());
    return VmValue::Void();
  };
}

IntrinsicHandler Flush(const bool close) {
  return [close](IntrinsicContext &call) {
    try {
      call.vm.IO().FlushOutput(call.receiver, close);
    } catch (const IoRuntimeError &error) {
      IoFailure(error);
    }
    return VmValue::Void();
  };
}

IntrinsicClassDecl DeclareFile() {
  auto builder =
      IntrinsicClassBuilder::Class("Ljava/io/File;", "Ljava/lang/Object;");
  builder.InstanceField("path", "Ljava/lang/String;");
  builder.Constructor("(Ljava/lang/String;)V", [](IntrinsicContext &call) {
    call.vm.Model().InstanceSlots(call.receiver)[0] = {
        call.arguments[0].ref.Value(), SlotTag::ref};
    return VmValue::Void();
  });
  builder.Constructor("(Ljava/lang/String;Ljava/lang/String;)V",
                      [](IntrinsicContext &call) {
                        auto path = call.vm.StringUtf8(call.arguments[0].ref);
                        if (!path.empty() && path.back() != '/')
                          path += '/';
                        path += call.vm.StringUtf8(call.arguments[1].ref);
                        call.vm.Model().InstanceSlots(call.receiver)[0] = {
                            call.vm.NewStringUtf8(path).Value(), SlotTag::ref};
                        return VmValue::Void();
                      });
  builder.FinalMethod("exists", "()Z", [](IntrinsicContext &call) {
    return VmValue::Int(
        call.vm.IO().Stat(FilePath(call, call.receiver)).has_value());
  });
  builder.FinalMethod("length", "()J", [](IntrinsicContext &call) {
    const auto info = call.vm.IO().Stat(FilePath(call, call.receiver));
    return VmValue::Long(
        info.has_value() ? static_cast<std::int64_t>(info->size) : 0);
  });
  const auto get_path = [](IntrinsicContext &call) {
    return VmValue::Ref(call.vm.NewStringUtf8(FilePath(call, call.receiver)));
  };
  builder.FinalMethod("getPath", "()Ljava/lang/String;", get_path);
  builder.FinalMethod("getAbsolutePath", "()Ljava/lang/String;", get_path);
  const auto make_directories = [](IntrinsicContext &call) {
    return VmValue::Int(
        call.vm.IO().MakeDirectories(FilePath(call, call.receiver)));
  };
  builder.FinalMethod("mkdir", "()Z", make_directories);
  builder.FinalMethod("mkdirs", "()Z", make_directories);
  builder.FinalMethod("createNewFile", "()Z", [](IntrinsicContext &call) {
    try {
      return VmValue::Int(
          call.vm.IO().CreateFile(FilePath(call, call.receiver)));
    } catch (const IoRuntimeError &error) {
      IoFailure(error);
    }
  });
  builder.FinalMethod("delete", "()Z", [](IntrinsicContext &call) {
    return VmValue::Int(call.vm.IO().Delete(FilePath(call, call.receiver)));
  });
  builder.FinalMethod("isDirectory", "()Z", [](IntrinsicContext &call) {
    const auto info = call.vm.IO().Stat(FilePath(call, call.receiver));
    return VmValue::Int(info.has_value() && info->is_directory);
  });
  builder.FinalMethod(
      "list", "()[Ljava/lang/String;", [](IntrinsicContext &call) {
        const auto names = call.vm.IO().List(FilePath(call, call.receiver));
        if (!names.has_value())
          return VmValue::Ref(VmObjectRef{});
        const auto array = call.vm.Model().NewObjectArray(
            call.vm.Linker().ResolveDescriptor("[Ljava/lang/String;"),
            call.vm.Linker().ResolveDescriptor("Ljava/lang/String;"),
            static_cast<JniSize>(names->size()));
        for (std::size_t index = 0; index < names->size(); ++index) {
          call.vm.Model().SetObjectElement(
              array, static_cast<JniSize>(index),
              call.vm.NewStringUtf8((*names)[index]));
        }
        return VmValue::Ref(array);
      });
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareFileInputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/FileInputStream;",
                                              "Ljava/io/InputStream;");
  builder.Constructor("(Ljava/io/File;)V", OpenInputFromPath(true));
  builder.Constructor("(Ljava/lang/String;)V", OpenInputFromPath(false));
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareFileReader() {
  auto builder =
      IntrinsicClassBuilder::Class("Ljava/io/FileReader;", "Ljava/io/Reader;");
  builder.Constructor("(Ljava/io/File;)V", OpenInputFromPath(true));
  builder.Constructor("(Ljava/lang/String;)V", OpenInputFromPath(false));
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareFileOutputStream() {
  auto builder = IntrinsicClassBuilder::Class("Ljava/io/FileOutputStream;",
                                              "Ljava/io/OutputStream;");
  builder.Constructor("(Ljava/io/File;)V", OpenOutputFromPath(true));
  builder.Constructor("(Ljava/lang/String;)V", OpenOutputFromPath(false));
  builder.FinalMethod("write", "([B)V", WriteBytes());
  builder.FinalMethod("flush", "()V", Flush(false));
  builder.FinalMethod("close", "()V", Flush(true));
  return std::move(builder).Build();
}

IntrinsicClassDecl DeclareFileWriter() {
  auto builder =
      IntrinsicClassBuilder::Class("Ljava/io/FileWriter;", "Ljava/io/Writer;");
  builder.Constructor("(Ljava/io/File;Z)V", [](IntrinsicContext &call) {
    const auto path = FilePath(call, call.arguments[0].ref);
    IoRuntime::OutputState output{path, {}, false};
    if (call.arguments[1].AsInt() != 0) {
      if (const auto existing = call.vm.IO().ReadFile(path)) {
        output.bytes = *existing;
      }
    }
    call.vm.IO().SetOutput(call.receiver, std::move(output));
    return VmValue::Void();
  });
  builder.FinalMethod(
      "append", "(C)Ljava/io/Writer;", [](IntrinsicContext &call) {
        IoRuntime::OutputState *output{};
        try {
          output = &call.vm.IO().Output(call.receiver);
        } catch (const IoRuntimeError &error) {
          IoFailure(error);
        }
        const auto unit =
            static_cast<char16_t>(call.arguments[0].cat1 & 0xffffU);
        const auto encoded = unit < 0x80U
                                 ? std::string(1, static_cast<char>(unit))
                                 : call.vm.StringUtf8(call.vm.Model().NewString(
                                       std::u16string(1, unit)));
        for (const auto value : encoded) {
          output->bytes.push_back(static_cast<std::byte>(value));
        }
        return VmValue::Ref(call.receiver);
      });
  builder.FinalMethod(
      "append", "(Ljava/lang/CharSequence;)Ljava/io/Writer;",
      [](IntrinsicContext &call) {
        IoRuntime::OutputState *output{};
        try {
          output = &call.vm.IO().Output(call.receiver);
        } catch (const IoRuntimeError &error) {
          IoFailure(error);
        }
        const auto value = call.arguments[0].ref;
        const auto text =
            value.IsValid() ? call.vm.StringUtf8(value) : std::string("null");
        for (const auto character : text) {
          output->bytes.push_back(static_cast<std::byte>(character));
        }
        return VmValue::Ref(call.receiver);
      });
  builder.FinalMethod("flush", "()V", Flush(false));
  builder.FinalMethod("close", "()V", Flush(true));
  return std::move(builder).Build();
}

} // namespace

void AppendJavaIoFiles(std::vector<IntrinsicClassDecl> &catalog) {
  catalog.push_back(DeclareFile());
  catalog.push_back(DeclareFileInputStream());
  catalog.push_back(DeclareFileOutputStream());
  catalog.push_back(DeclareFileReader());
  catalog.push_back(DeclareFileWriter());
}

} // namespace ogplay::runtime::dexvm::intrinsics
