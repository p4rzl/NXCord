cmake_minimum_required(VERSION 3.20)
project(nxcord_dave_tests LANGUAGES C CXX)
enable_testing()
add_subdirectory(../../externals/dave dave)
FetchContent_GetProperties(dave_source)
add_executable(test_dave test_dave.cpp
    ${dave_source_SOURCE_DIR}/cpp/test/external_sender.cpp)
target_include_directories(test_dave PRIVATE ${dave_source_SOURCE_DIR}/cpp/test
    ${dave_source_SOURCE_DIR}/cpp/src)
target_link_libraries(test_dave PRIVATE nxcord_voice_crypto)
target_compile_definitions(test_dave PRIVATE WITH_OPENSSL3)
target_compile_options(test_dave PRIVATE -UNDEBUG)
add_test(NAME dave_mls_and_opus COMMAND test_dave)
