#include "catalog.h"

namespace ogplay::runtime::dexvm {

std::vector<IntrinsicClassDecl> CoreIntrinsicCatalog(
    CoreIntrinsicServices services) {
    using namespace intrinsics;
    std::vector<IntrinsicClassDecl> catalog{
        Declare_java_lang_Object(),
        Declare_java_lang_ClassLoader(),
        Declare_java_lang_BootClassLoader(),
        Declare_dalvik_system_PathClassLoader(),
        Declare_java_lang_Thread(),
        Declare_java_lang_Thread_UncaughtExceptionHandler(),
        Declare_java_lang_ThreadGroup(),
        Declare_java_lang_StackTraceElement(),
        Declare_java_lang_Thread_State(),
        Declare_java_lang_Enum(),
        Declare_java_io_Serializable(),
        Declare_java_lang_String(),
        Declare_java_lang_StringBuilder(),
        Declare_java_lang_StringBuffer(),
        Declare_java_lang_System(),
        Declare_java_io_PrintStream(),
        Declare_java_lang_Math(),
        Declare_java_util_Random(),
        Declare_java_util_Date(),
        Declare_java_lang_Class(),
        Declare_java_lang_reflect_AnnotatedElement(),
        Declare_java_lang_reflect_GenericDeclaration(),
        Declare_java_lang_reflect_Type(),
        Declare_java_lang_reflect_Member(),
        Declare_java_lang_reflect_AccessibleObject(),
        Declare_java_lang_reflect_Modifier(),
        Declare_java_lang_reflect_Method(),
        Declare_java_lang_reflect_InvocationTargetException(),
        Declare_java_lang_reflect_Constructor(),
        Declare_java_lang_reflect_Field(),
        Declare_java_lang_ref_WeakReference(),
        Declare_java_lang_reflect_Array(),
    };

    AppendJavaLangInterfaces(catalog);
    AppendJavaLangPrimitiveWrappers(catalog);
    AppendJavaUtilCollections(catalog);
    AppendJavaIoStreams(catalog);
    AppendJavaIoFiles(catalog);
    AppendJavaUtilZip(catalog);

    AppendJavaLangThrowables(catalog);

    catalog.insert(catalog.end(), {
        Declare_java_io_IOException(),
        Declare_java_io_FileNotFoundException(),
        Declare_java_io_UnsupportedEncodingException(),
        Declare_java_util_regex_PatternSyntaxException(),
        Declare_java_net_SocketException(),
        Declare_java_net_SocketTimeoutException(),
        Declare_java_net_UnknownHostException(),
        Declare_java_net_MalformedURLException(),
        Declare_java_io_EOFException(),
    });

    AppendJavaNetPlatform(catalog, services);
    AppendJavaNio(catalog);
    AppendJavaUtilPlatform(catalog, services);
    AppendJavaUtilAlgorithms(catalog, services);
    AppendJavaText(catalog);
    AppendJavaRegex(catalog);
    AppendJavaConcurrent(catalog);
    AppendJavaXml(catalog, services);

    return catalog;
}

}  // namespace ogplay::runtime::dexvm
