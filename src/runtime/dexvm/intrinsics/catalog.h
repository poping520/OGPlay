#pragma once

#include <vector>

#include "ogplay/runtime/dexvm/class_linker.h"

namespace ogplay::runtime::dexvm::intrinsics {

void AppendJavaLangThrowables(std::vector<IntrinsicClassDecl>& catalog);
void AppendJavaLangPrimitiveWrappers(
    std::vector<IntrinsicClassDecl>& catalog);
void AppendJavaLangInterfaces(std::vector<IntrinsicClassDecl>& catalog);

[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Object();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_ClassLoader();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_BootClassLoader();
[[nodiscard]] IntrinsicClassDecl Declare_dalvik_system_PathClassLoader();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Thread();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Enum();
[[nodiscard]] IntrinsicClassDecl Declare_java_io_Serializable();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_String();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_StringBuilder();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_StringBuffer();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_System();
[[nodiscard]] IntrinsicClassDecl Declare_java_io_PrintStream();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Math();
[[nodiscard]] IntrinsicClassDecl Declare_java_util_HashMap();
[[nodiscard]] IntrinsicClassDecl Declare_java_util_Hashtable();
[[nodiscard]] IntrinsicClassDecl Declare_java_util_List();
[[nodiscard]] IntrinsicClassDecl Declare_java_util_Vector();
[[nodiscard]] IntrinsicClassDecl Declare_java_util_Stack();
[[nodiscard]] IntrinsicClassDecl Declare_java_util_ArrayList();
[[nodiscard]] IntrinsicClassDecl Declare_java_util_Random();
[[nodiscard]] IntrinsicClassDecl Declare_java_util_Date();
[[nodiscard]] IntrinsicClassDecl Declare_java_util_Iterator();
[[nodiscard]] IntrinsicClassDecl Declare_java_util_CollectionIterator();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Class();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_AnnotatedElement();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_GenericDeclaration();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_Type();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_Member();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_AccessibleObject();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_Modifier();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_Method();
[[nodiscard]] IntrinsicClassDecl
Declare_java_lang_reflect_InvocationTargetException();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_Constructor();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_Field();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_ref_WeakReference();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_Array();
[[nodiscard]] IntrinsicClassDecl Declare_java_io_IOException();
[[nodiscard]] IntrinsicClassDecl Declare_java_io_FileNotFoundException();
[[nodiscard]] IntrinsicClassDecl Declare_java_io_UnsupportedEncodingException();
[[nodiscard]] IntrinsicClassDecl Declare_java_util_regex_PatternSyntaxException();
[[nodiscard]] IntrinsicClassDecl Declare_java_util_NoSuchElementException();
[[nodiscard]] IntrinsicClassDecl Declare_java_util_EmptyStackException();
[[nodiscard]] IntrinsicClassDecl Declare_java_net_SocketException();
[[nodiscard]] IntrinsicClassDecl Declare_java_net_SocketTimeoutException();
[[nodiscard]] IntrinsicClassDecl Declare_java_net_UnknownHostException();
[[nodiscard]] IntrinsicClassDecl Declare_java_net_MalformedURLException();
[[nodiscard]] IntrinsicClassDecl Declare_java_io_EOFException();

}  // namespace ogplay::runtime::dexvm::intrinsics
