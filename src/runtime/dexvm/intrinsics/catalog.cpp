#include "catalog.h"

namespace ogplay::runtime::dexvm {

std::vector<IntrinsicClassDecl> CoreIntrinsicCatalog() {
    using namespace intrinsics;
    std::vector<IntrinsicClassDecl> catalog{
        Declare_java_lang_Object(),
        Declare_java_lang_Runnable(),
        Declare_java_lang_CharSequence(),
        Declare_java_lang_Comparable(),
        Declare_java_io_Serializable(),
        Declare_java_lang_String(),
        Declare_java_lang_StringBuilder(),
        Declare_java_lang_StringBuffer(),
        Declare_java_lang_System(),
        Declare_java_io_PrintStream(),
        Declare_java_lang_Math(),
        Declare_java_util_HashMap(),
        Declare_java_util_Hashtable(),
        Declare_java_util_List(),
        Declare_java_util_Vector(),
        Declare_java_util_Stack(),
        Declare_java_util_ArrayList(),
        Declare_java_util_Random(),
        Declare_java_util_Date(),
        Declare_java_util_Iterator(),
        Declare_java_util_CollectionIterator(),
        Declare_java_lang_Class(),
        Declare_java_lang_reflect_Method(),
        Declare_java_lang_ref_WeakReference(),
        Declare_java_lang_reflect_Array(),
    };

    AppendJavaLangPrimitiveWrappers(catalog);

    AppendJavaLangThrowables(catalog);

    catalog.insert(catalog.end(), {
        Declare_java_io_IOException(),
        Declare_java_io_FileNotFoundException(),
        Declare_java_io_UnsupportedEncodingException(),
        Declare_java_util_regex_PatternSyntaxException(),
        Declare_java_util_NoSuchElementException(),
        Declare_java_util_EmptyStackException(),
        Declare_java_net_SocketException(),
        Declare_java_net_SocketTimeoutException(),
        Declare_java_net_UnknownHostException(),
        Declare_java_net_MalformedURLException(),
        Declare_java_io_EOFException(),
    });

    return catalog;
}

}  // namespace ogplay::runtime::dexvm
