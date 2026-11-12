#include "catalog.h"
#include <stdexcept>

namespace ogplay::runtime::dexvm {

std::vector<IntrinsicClassDecl> CoreIntrinsicCatalog(CoreIntrinsicServices services) {
    using namespace intrinsics;
    if (services.cpu_cores < 1 || services.cpu_cores > 32)
        throw std::invalid_argument("guest CPU requires 1..32 cores");
    std::vector<IntrinsicClassDecl> catalog{
        Declare_java_lang_Object_Class(),
        Declare_java_lang_VMClassLoader_Natives(services),
        Declare_java_lang_reflect_Proxy_Natives(),
        Declare_dalvik_system_VMStack_Natives(),
        Declare_dalvik_system_BaseDexClassLoader_Shell(),
        Declare_dalvik_system_PathClassLoader_Shell(),
        Declare_java_lang_Thread_Class(),
        Declare_java_lang_ThreadGroup_Class(),
        Declare_java_lang_String_Class(),
        Declare_java_lang_RealToString_Natives(),
        Declare_java_lang_StringToReal_Natives(),
        Declare_java_lang_System_Class(services),
        Declare_org_ogplay_io_LogOutputStream_Natives(),
        Declare_java_lang_Math_Natives(),
        Declare_java_lang_Runtime_Overrides(services),
        Declare_libcore_io_IoUtils_Overrides(),
        Declare_java_lang_Class_Natives(services),
        Declare_java_lang_reflect_AccessibleObject_Natives(),
        Declare_java_lang_reflect_Method_Natives(),
        Declare_java_lang_reflect_Constructor_Natives(),
        Declare_java_lang_reflect_Field_Natives(),
        Declare_java_lang_reflect_Array_Natives(),
    };

    AppendDalvikVmRuntimeNatives(catalog);
    AppendJavaLangWrapperNatives(catalog);
    AppendJavaIoStreams(catalog);
    AppendJavaIoFiles(catalog);
    AppendJavaUtilZip(catalog);

    AppendJavaLangThrowableNatives(catalog);

    AppendJavaNetPlatform(catalog, services);
    AppendJavaNio(catalog);
    AppendJavaIcu(catalog, services);
    AppendJavaCrypto(catalog, services);
    AppendJavaRegex(catalog);
    AppendJavaConcurrent(catalog, services);
    AppendJavaXml(catalog);

    return catalog;
}

}  // namespace ogplay::runtime::dexvm
