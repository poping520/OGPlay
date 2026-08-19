#include <string>

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FileOutputStream(const Context& context) {
    auto builder = dx::IntrinsicClassBuilder::Class("Ljava/io/FileOutputStream;", "Ljava/io/OutputStream;");
    const auto open_output = [context](dx::IntrinsicContext& call,
                                       const std::string& path) {
        context->output_streams[call.receiver.Value()] =
            DexVmAndroidContext::OutputStream{path, {}, false};
        return dx::VmValue::Void();
    };
    builder.Constructor("(Ljava/lang/String;)V",
        [open_output](dx::IntrinsicContext& call) {
            return open_output(call,
                               call.vm.StringUtf8(call.arguments[0].ref));
        });
    builder.Constructor("(Ljava/io/File;)V",
        [open_output](dx::IntrinsicContext& call) {
            const auto slots =
                call.vm.Model().InstanceSlots(call.arguments[0].ref);
            return open_output(
                call, call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits)));
        });
    builder.FinalMethod("write", "([B)V", FileOutputWriteBytesHandler(context));
    builder.FinalMethod("flush", "()V", FileOutputFlushHandler(context));
    builder.FinalMethod("close", "()V", FileOutputCloseHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
