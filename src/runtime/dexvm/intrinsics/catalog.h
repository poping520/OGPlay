#pragma once

#include <vector>
#include <span>
#include <string>
#include <string_view>

#include "ogplay/runtime/dexvm/class_linker.h"

namespace ogplay::runtime::dexvm::intrinsics {

std::string CanonicalCharset(std::string name);
std::string CharsetName(Interpreter& vm, VmObjectRef charset);
std::u16string DecodeCharset(std::span<const std::byte> bytes, const std::string& charset);
std::vector<std::byte> EncodeCharset(std::u16string_view text, const std::string& charset);

void AppendJavaCrypto(std::vector<IntrinsicClassDecl>& catalog, const CoreIntrinsicServices& services);
void AppendJavaLangThrowableNatives(std::vector<IntrinsicClassDecl>& catalog);
void AppendJavaLangWrapperNatives(std::vector<IntrinsicClassDecl>& catalog);
void AppendDalvikVmRuntimeNatives(std::vector<IntrinsicClassDecl>& catalog);
void AppendJavaIoStreams(std::vector<IntrinsicClassDecl>& catalog);
void AppendJavaIoFiles(std::vector<IntrinsicClassDecl>& catalog);
void AppendJavaUtilZip(std::vector<IntrinsicClassDecl>& catalog);
void AppendJavaIcu(std::vector<IntrinsicClassDecl>& catalog, const CoreIntrinsicServices& services);
void AppendJavaRegex(std::vector<IntrinsicClassDecl>& catalog);
void AppendJavaConcurrent(std::vector<IntrinsicClassDecl>& catalog, const CoreIntrinsicServices& services);
void AppendJavaNio(std::vector<IntrinsicClassDecl>& catalog);
void AppendJavaNetPlatform(std::vector<IntrinsicClassDecl>& catalog, const CoreIntrinsicServices& services);
void AppendJavaXml(std::vector<IntrinsicClassDecl>& catalog);

[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Object_Class();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_VMClassLoader_Natives(const CoreIntrinsicServices& services);
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_Proxy_Natives();
[[nodiscard]] IntrinsicClassDecl Declare_dalvik_system_VMStack_Natives();
[[nodiscard]] IntrinsicClassDecl Declare_dalvik_system_BaseDexClassLoader_Shell();
[[nodiscard]] IntrinsicClassDecl Declare_dalvik_system_PathClassLoader_Shell();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Thread_Class();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_ThreadGroup_Class();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_String_Class();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_RealToString_Natives();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_StringToReal_Natives();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_System_Class(const CoreIntrinsicServices& services);
[[nodiscard]] IntrinsicClassDecl Declare_org_ogplay_io_LogOutputStream_Natives();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Math_Natives();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Runtime_Overrides(const CoreIntrinsicServices& services);
[[nodiscard]] IntrinsicClassDecl Declare_libcore_io_IoUtils_Overrides();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_Class_Natives(const CoreIntrinsicServices& services);
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_AccessibleObject_Natives();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_Method_Natives();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_Constructor_Natives();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_Field_Natives();
[[nodiscard]] IntrinsicClassDecl Declare_java_lang_reflect_Array_Natives();
[[nodiscard]] IntrinsicClassDecl Declare_java_io_FileNotFoundException_Class();

}  // namespace ogplay::runtime::dexvm::intrinsics
