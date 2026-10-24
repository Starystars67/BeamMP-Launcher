cmake_minimum_required(VERSION 3.10)

project(Launcher)

if (WIN32)
    message(STATUS "MSVC -> forcing use of statically-linked runtime.")
    STRING(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_RELEASE ${CMAKE_CXX_FLAGS_RELEASE})
    STRING(REPLACE "/MDd" "/MTd" CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})
endif(WIN32)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")

add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT)

file(GLOB source_files "src/*.cpp" "src/*/*.cpp" "src/*/*.hpp" "include/*.h"  "include/*/*.h" "include/*/*/*.h" "include/*.hpp"  "include/*/*.hpp" "include/*/*/*.hpp")
find_package(httplib CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(CURL REQUIRED)

set(BEAMMP_EXECUTABLE_NAME "BeamMP-Launcher" CACHE STRING "Executable name, without extension")

add_executable(${PROJECT_NAME} ${source_files})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${BEAMMP_EXECUTABLE_NAME}")
target_compile_definitions(${PROJECT_NAME} PRIVATE "BEAMMP_EXECUTABLE_NAME=\"${BEAMMP_EXECUTABLE_NAME}\"")

if (WIN32)
    find_package(ZLIB REQUIRED)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto ws2_32 httplib::httplib nlohmann_json::nlohmann_json CURL::libcurl)
elseif (UNIX)
    find_package(ZLIB REQUIRED)
    find_package(OpenSSL REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE
            ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto CURL::libcurl nlohmann_json::nlohmann_json)
    # header only; take just the include path so a zlib-enabled httplib package doesn't clash with the bundled miniz
    get_target_property(HTTPLIB_INCLUDE_DIRS httplib::httplib INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(${PROJECT_NAME} PRIVATE ${HTTPLIB_INCLUDE_DIRS})
    if (APPLE)
        # httplib reads the system keychain certificates for TLS
        target_link_libraries(${PROJECT_NAME} PRIVATE "-framework CoreFoundation" "-framework Security")
    endif(APPLE)
else(WIN32) #MINGW
    add_definitions("-D_WIN32_WINNT=0x0600")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Os -s --static")
    target_link_libraries(${PROJECT_NAME} ssl crypto ws2_32 ssp crypt32 z CURL::libcurl)
endif(WIN32)
target_include_directories(${PROJECT_NAME} PRIVATE "include")

option(BEAMMP_BUILD_TESTS "Build the unit tests" OFF)
if (BEAMMP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
