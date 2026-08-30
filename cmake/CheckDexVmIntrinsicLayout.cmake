if(POLICY CMP0057)
    cmake_policy(SET CMP0057 NEW)
endif()

if(NOT DEFINED ROOT)
    message(FATAL_ERROR "ROOT is required")
endif()

set(core_dir "${ROOT}/src/runtime/dexvm/intrinsics")
set(android_dir "${ROOT}/src/runtime/integration/dexvm_android")

set(core_allowlist
    catalog.cpp
    java_classloading.cpp
    java_concurrent.cpp
    java_io.cpp
    java_lang.cpp
    java_net.cpp
    java_nio.cpp
    java_reflect.cpp
    java_regex.cpp
    java_util.cpp
    java_xml.cpp
    java_zip.cpp)
set(android_allowlist
    android_app.cpp
    android_content.cpp
    android_device.cpp
    android_gl.cpp
    android_graphics.cpp
    android_hardware.cpp
    android_media.cpp
    android_network.cpp
    android_os.cpp
    android_resources.cpp
    android_text.cpp
    android_util.cpp
    android_view.cpp
    android_webkit.cpp
    android_widget.cpp
    catalog.cpp
    shared.cpp
    support_ui.cpp)

foreach(kind IN ITEMS core android)
    file(GLOB sources RELATIVE "${${kind}_dir}" "${${kind}_dir}/*.cpp")
    list(SORT sources)
    set(expected "${${kind}_allowlist}")
    list(SORT expected)
    if(NOT sources STREQUAL expected)
        message(FATAL_ERROR
            "DexVM ${kind} intrinsic layout drift: expected [${expected}], found [${sources}]")
    endif()
    foreach(source IN LISTS sources)
        if(source MATCHES "(_misc|_common|_all)\\.cpp$")
            message(FATAL_ERROR "Forbidden intrinsic catch-all: ${kind}/${source}")
        endif()
    endforeach()
endforeach()

file(READ "${core_dir}/catalog.cpp" core_catalog)
file(READ "${android_dir}/catalog.cpp" android_catalog)
foreach(token IN ITEMS IntrinsicClassBuilder IntrinsicHandler VmJavaThrow)
    if(core_catalog MATCHES "${token}" OR android_catalog MATCHES "${token}")
        message(FATAL_ERROR "catalog.cpp contains behavior token: ${token}")
    endif()
endforeach()

file(GLOB core_files "${core_dir}/*.cpp" "${core_dir}/*.h")
foreach(source IN LISTS core_files)
    file(READ "${source}" content)
    if(content MATCHES "DexVmAndroidContext|runtime/integration/dexvm_android")
        message(FATAL_ERROR "Core intrinsic depends on Android context: ${source}")
    endif()
endforeach()

file(GLOB android_files "${android_dir}/*.cpp")
foreach(source IN LISTS android_files)
    file(READ "${source}" content)
    string(REGEX MATCHALL
        "IntrinsicClassBuilder::(Class|Interface)\\([ \\t\\r\\n]*\"L(java/|javax/net/|javax/xml/|org/xml/)"
        forbidden_declarations "${content}")
    if(forbidden_declarations)
        message(FATAL_ERROR
            "Non-Android Java declaration remains in Android integration: ${source}")
    endif()
endforeach()

set(required_core_descriptors
    "Ljava/net/URL;"
    "Ljava/nio/charset/Charset;"
    "Ljava/util/Locale;"
    "Ljava/util/Timer;"
    "Ljavax/net/ssl/SSLContext;"
    "Ljavax/xml/parsers/SAXParser;"
    "Lorg/xml/sax/XMLReader;")
file(READ "${core_dir}/java_net.cpp" java_net)
file(READ "${core_dir}/java_nio.cpp" java_nio)
file(READ "${core_dir}/java_util.cpp" java_util)
file(READ "${core_dir}/java_xml.cpp" java_xml)
set(core_platform_text "${java_net}${java_nio}${java_util}${java_xml}")
foreach(descriptor IN LISTS required_core_descriptors)
    string(FIND "${core_platform_text}" "${descriptor}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Missing core-owned descriptor: ${descriptor}")
    endif()
endforeach()

# DVM-94/95 architecture contracts. Link-time validation is the semantic
# gate; these checks prevent the explicit APIs and the canonical Application
# inheritance example from being mechanically removed.
file(READ "${ROOT}/include/ogplay/runtime/dexvm/class_linker.h" linker_header)
file(READ "${ROOT}/include/ogplay/runtime/dexvm/intrinsic_builder.h" builder_header)
file(READ "${ROOT}/include/ogplay/runtime/dexvm/owned_state_table.h" state_header)
foreach(token IN ITEMS "enum class InvokeKind" "struct MethodShape" "ResolvedCallSite")
    string(FIND "${linker_header}" "${token}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Missing DVM-94 linker contract: ${token}")
    endif()
endforeach()
foreach(token IN ITEMS "OverrideMethod" "UnimplementedOverride")
    string(FIND "${builder_header}" "${token}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "Missing DVM-94 builder contract: ${token}")
    endif()
endforeach()
string(FIND "${state_header}" "class OwnedStateTable" found)
if(found EQUAL -1)
    message(FATAL_ERROR "Missing DVM-95 OwnedStateTable contract")
endif()

file(READ "${android_dir}/android_app.cpp" android_app)
foreach(copied_method IN ITEMS "getPackageName" "getAssets" "getFilesDir")
    string(FIND "${android_app}" "${copied_method}" found)
    if(NOT found EQUAL -1)
        message(FATAL_ERROR
            "Application declaration copied inherited Context method: ${copied_method}")
    endif()
endforeach()

# DVM-94 regression: Android 4.4 protected subclass callbacks must never fall
# back to IntrinsicClassBuilder's public default. Counts are lower bounds so
# adding another audited protected API does not break the gate.
file(READ "${android_dir}/shared.h" android_shared)
string(FIND "${android_shared}" "kProtectedAccess" protected_access_found)
if(protected_access_found EQUAL -1)
    message(FATAL_ERROR "Missing explicit Android protected access contract")
endif()
foreach(source_and_minimum IN ITEMS
        "android_app.cpp:8"
        "android_content.cpp:1"
        "android_os.cpp:7"
        "android_view.cpp:1")
    string(REPLACE ":" ";" pair "${source_and_minimum}")
    list(GET pair 0 source)
    list(GET pair 1 minimum)
    file(READ "${android_dir}/${source}" protected_source)
    string(REGEX MATCHALL "kProtectedAccess" protected_uses
           "${protected_source}")
    list(LENGTH protected_uses protected_count)
    if(protected_count LESS minimum)
        message(FATAL_ERROR
            "Android protected callback metadata regressed in ${source}: expected at least ${minimum}, found ${protected_count}")
    endif()
endforeach()

file(READ "${ROOT}/tests/dexvm/videoview_tests.cpp" android_contract_tests)
string(FIND "${android_contract_tests}"
       "Android 4.4 override callbacks preserve framework visibility"
       visibility_test_found)
if(visibility_test_found EQUAL -1)
    message(FATAL_ERROR "Missing Android override visibility regression test")
endif()
