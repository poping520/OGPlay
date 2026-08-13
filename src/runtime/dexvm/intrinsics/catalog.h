#pragma once

#include "ogplay/runtime/dexvm/class_linker.h"

namespace ogplay::runtime::dexvm::intrinsics {

[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Object();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Runnable();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_CharSequence();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Comparable();
[[nodiscard]] IntrinsicClassDecl Declare_java_io_Serializable();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_String();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_StringBuilder();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_StringBuffer();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_System();
[[nodiscard]] IntrinsicClassDecl Declare_java_io_PrintStream();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Math();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Number();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Integer();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Long();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Boolean();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Float();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Double();
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
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_Method();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_ref_WeakReference();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_Array();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Throwable();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Exception();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_RuntimeException();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_NullPointerException();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_ArithmeticException();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_IndexOutOfBoundsException();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_ArrayIndexOutOfBoundsException();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_StringIndexOutOfBoundsException();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_ClassCastException();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_NegativeArraySizeException();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_ArrayStoreException();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_IllegalMonitorStateException();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_IllegalArgumentException();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_IllegalStateException();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_UnsupportedOperationException();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_ClassNotFoundException();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_InterruptedException();
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
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_NumberFormatException();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_IllegalThreadStateException();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Error();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_LinkageError();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_NoClassDefFoundError();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_UnsatisfiedLinkError();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_VirtualMachineError();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_StackOverflowError();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_OutOfMemoryError();

}  // namespace ogplay::runtime::dexvm::intrinsics
