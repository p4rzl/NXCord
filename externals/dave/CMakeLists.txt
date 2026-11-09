cmake_minimum_required(VERSION 3.20)

# Pinned upstream sources; never use a host libcrypto in the Switch binary.
include(FetchContent)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
FetchContent_Declare(dave_source
    URL https://codeload.github.com/discord/libdave/tar.gz/6396c90aedbb9a6ee117ea9ce1ce9832af6a3375
    URL_HASH SHA256=37784dd0c1476738426f0968a6420dc8590e4b40cb8aae42e36df3c663e18cc5
    SOURCE_SUBDIR nxcord-unused)
FetchContent_Declare(mls_source
    URL https://codeload.github.com/cisco/mlspp/tar.gz/1cc50a124a3bc4e143a787ec934280dc70c1034d
    URL_HASH SHA256=7a9d6318627e548903bc65c3dc5a4de4a90290983efea9a49af8561ed3f999f5
    SOURCE_SUBDIR nxcord-unused)
FetchContent_Declare(dave_json
    URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.12.0
    URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
    SOURCE_SUBDIR nxcord-unused)
FetchContent_MakeAvailable(dave_source mls_source dave_json)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/licenses")
configure_file("${dave_source_SOURCE_DIR}/LICENSE" "${CMAKE_BINARY_DIR}/licenses/libdave.txt" COPYONLY)
configure_file("${mls_source_SOURCE_DIR}/LICENSE" "${CMAKE_BINARY_DIR}/licenses/mlspp.txt" COPYONLY)
configure_file("${dave_json_SOURCE_DIR}/LICENSE.MIT" "${CMAKE_BINARY_DIR}/licenses/nlohmann-json.txt" COPYONLY)

if(SWITCH)
    FetchContent_Declare(dave_openssl
        URL https://codeload.github.com/openssl/openssl/tar.gz/refs/tags/openssl-3.5.9
        URL_HASH SHA256=796831ec533bf41998eaaee9d632d921b8a69f57394fa4086ff01a941113d26d
        SOURCE_SUBDIR nxcord-unused)
    FetchContent_MakeAvailable(dave_openssl)
    configure_file("${dave_openssl_SOURCE_DIR}/LICENSE.txt" "${CMAKE_BINARY_DIR}/licenses/openssl.txt" COPYONLY)
    include(ExternalProject)
    find_program(DAVE_MAKE make REQUIRED)
    find_package(Perl REQUIRED)
    # No Linux syscalls, dynamic engines, sockets, or OS entropy paths. Random
    # bytes are provided by the Switch csrng service (see dave_crypto.cpp).
    ExternalProject_Add(dave_crypto_build
        SOURCE_DIR "${dave_openssl_SOURCE_DIR}"
        BINARY_DIR "${dave_openssl_BINARY_DIR}"
        DOWNLOAD_COMMAND ""
        PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=<SOURCE_DIR>
            -P ${CMAKE_CURRENT_LIST_DIR}/openssl_switch.cmake
        CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env
            "CC=${CMAKE_C_COMPILER}" "AR=${CMAKE_AR}" "RANLIB=${CMAKE_RANLIB}"
            ${PERL_EXECUTABLE} <SOURCE_DIR>/Configure gcc
            no-shared no-tests no-apps no-docs no-asm no-threads no-async
            no-sock no-dso no-module no-engine no-ui-console no-posix-io
            --with-rand-seed=none
            -D__SWITCH__ -DOPENSSL_NO_SECURE_MEMORY -DNO_SYSLOG
            -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE
            -ffunction-sections -fdata-sections -O2
        BUILD_COMMAND ${DAVE_MAKE} -j4 build_generated libcrypto.a
        INSTALL_COMMAND ""
        BUILD_BYPRODUCTS "${dave_openssl_BINARY_DIR}/libcrypto.a")
    file(MAKE_DIRECTORY "${dave_openssl_BINARY_DIR}/include")
    add_library(dave_crypto STATIC IMPORTED GLOBAL)
    set_target_properties(dave_crypto PROPERTIES
        IMPORTED_LOCATION "${dave_openssl_BINARY_DIR}/libcrypto.a"
        INTERFACE_INCLUDE_DIRECTORIES "${dave_openssl_BINARY_DIR}/include;${dave_openssl_SOURCE_DIR}/include")
    add_dependencies(dave_crypto dave_crypto_build)
else()
    find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
    add_library(dave_crypto ALIAS OpenSSL::Crypto)
endif()

# Build only the MLS/HPKE libraries used by libdave (not mls_ds or test vectors).
# MLS and libdave require exceptions internally. Do not change the application's
# existing -fno-exceptions ABI/policy globally.
set(MLS_CXX_NAMESPACE mlspp)
configure_file("${mls_source_SOURCE_DIR}/cmake/namespace.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/namespace.h" @ONLY)
foreach(part tls_syntax bytes hpke)
    file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${mls_source_SOURCE_DIR}/lib/${part}/src/*.cpp")
    add_library(dave_${part} STATIC ${sources})
    target_include_directories(dave_${part} PUBLIC
        "${mls_source_SOURCE_DIR}/lib/${part}/include"
        "${mls_source_SOURCE_DIR}/include"
        "${mls_source_SOURCE_DIR}/third_party"
        "${CMAKE_CURRENT_BINARY_DIR}/include"
        "${dave_json_SOURCE_DIR}/single_include")
endforeach()
add_library(nxcord_voice_crypto STATIC
    ${CMAKE_CURRENT_LIST_DIR}/../../nxcord/src/dave_session.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../nxcord/src/dave_crypto.cpp)
target_include_directories(nxcord_voice_crypto PUBLIC ${CMAKE_CURRENT_LIST_DIR}/../../include)
target_link_libraries(nxcord_voice_crypto PUBLIC nxcord_dave)
target_compile_options(nxcord_voice_crypto PRIVATE -fexceptions)
if(SWITCH)
    target_link_libraries(nxcord_voice_crypto PUBLIC switch::libnx)
endif()
target_link_libraries(dave_bytes PUBLIC dave_tls_syntax)
target_link_libraries(dave_hpke PUBLIC dave_bytes dave_tls_syntax dave_crypto)
file(GLOB_RECURSE mls_sources CONFIGURE_DEPENDS "${mls_source_SOURCE_DIR}/src/*.cpp")
add_library(dave_mls STATIC ${mls_sources})
target_link_libraries(dave_mls PUBLIC dave_hpke)

file(GLOB_RECURSE dave_sources CONFIGURE_DEPENDS "${dave_source_SOURCE_DIR}/cpp/src/*.cpp")
list(FILTER dave_sources EXCLUDE REGEX "(_win|_apple|_wasm|boringssl_cryptor|persisted_key).*\\.cpp$")
list(APPEND dave_sources "${dave_source_SOURCE_DIR}/cpp/src/mls/persisted_key_pair_null.cpp")
add_library(nxcord_dave STATIC ${dave_sources})
target_include_directories(nxcord_dave PUBLIC "${dave_source_SOURCE_DIR}/cpp/includes"
    PRIVATE "${dave_source_SOURCE_DIR}/cpp/src")
target_link_libraries(nxcord_dave PUBLIC dave_mls)
foreach(target dave_tls_syntax dave_bytes dave_hpke dave_mls nxcord_dave)
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_compile_options(${target} PRIVATE -fexceptions -ffunction-sections -fdata-sections)
    target_compile_definitions(${target} PRIVATE WITH_OPENSSL3 DISABLE_GREASE DISABLE_PQ)
endforeach()
