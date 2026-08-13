#include <string>

#include "catalog.h"

namespace ogplay::runtime::android_intrinsics {

Decl Declare_java_io_FileOutputStream(const Context& context) {
    dx::IntrinsicClassBuilder builder("Ljava/io/FileOutputStream;");
    builder.Super("Ljava/io/OutputStream;");
    const auto open_output = [context](dx::IntrinsicContext& call,
                                       const std::string& path) {
        context->output_streams[call.receiver.Value()] =
            DexVmAndroidContext::OutputStream{path, {}, false};
        return dx::VmValue::Void();
    };
    builder.Virtual("<init>", "(Ljava/lang/String;)V",
        [open_output](dx::IntrinsicContext& call) {
            return open_output(call,
                               call.vm.StringUtf8(call.arguments[0].ref));
        });
    builder.Virtual("<init>", "(Ljava/io/File;)V",
        [open_output](dx::IntrinsicContext& call) {
            const auto slots =
                call.vm.Model().InstanceSlots(call.arguments[0].ref);
            return open_output(
                call, call.vm.StringUtf8(dx::VmObjectRef(slots[0].bits)));
        });
    builder.Virtual("write", "([B)V", FileOutputWriteBytesHandler(context));
    builder.Virtual("flush", "()V", FileOutputFlushHandler(context));
    builder.Virtual("close", "()V", FileOutputCloseHandler(context));
    return std::move(builder).Build();
}

}  // namespace ogplay::runtime::android_intrinsics
