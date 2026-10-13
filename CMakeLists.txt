cmake_minimum_required(VERSION 3.24)

project(spinneret
    VERSION 0.1.0
    DESCRIPTION "XML processing middleware for airport integration"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(SPINNERET_WITH_IBMMQ "Build the IBM MQ adapters (requires the IBM MQ client SDK)" OFF)
option(SPINNERET_WITH_TLS "Enable HTTPS for the webhook output when OpenSSL is available" ON)
option(SPINNERET_BUILD_TESTS "Build the test suite" ON)
set(SPINNERET_MQ_HOME "" CACHE PATH "IBM MQ client installation directory")

# ── Dependencies ─────────────────────────────────────────────────────────────

find_package(Threads REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(nlohmann_json 3.11 CONFIG REQUIRED)

find_package(httplib CONFIG QUIET)
if(NOT httplib_FOUND)
    include(FetchContent)
    set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE ${SPINNERET_WITH_TLS} CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG        v0.20.1
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(httplib)
endif()

# ── Generated sources ────────────────────────────────────────────────────────

set(SPINNERET_GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
configure_file(src/spinneret/version.h.in ${SPINNERET_GENERATED_DIR}/spinneret/version.h @ONLY)

file(READ resources/openapi.json SPINNERET_OPENAPI_JSON)
configure_file(src/spinneret/http/openapi_spec.cpp.in ${SPINNERET_GENERATED_DIR}/openapi_spec.cpp @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS resources/openapi.json)

# ── Core library ─────────────────────────────────────────────────────────────

add_library(spinneret_core STATIC
    src/spinneret/util/logging.cpp
    src/spinneret/util/random.cpp
    src/spinneret/util/sha256.cpp
    src/spinneret/util/strings.cpp
    src/spinneret/util/files.cpp
    src/spinneret/util/time.cpp
    src/spinneret/domain/correlation_metadata.cpp
    src/spinneret/domain/flight_event.cpp
    src/spinneret/domain/message_types.cpp
    src/spinneret/pipeline/diagnostics.cpp
    src/spinneret/pipeline/header_extractor.cpp
    src/spinneret/pipeline/intent_resolver.cpp
    src/spinneret/pipeline/payload_extractor.cpp
    src/spinneret/pipeline/pipeline_context.cpp
    src/spinneret/pipeline/pipeline_engine.cpp
    src/spinneret/pipeline/pipeline_error.cpp
    src/spinneret/pipeline/transformers.cpp
    src/spinneret/pipeline/xml_format.cpp
    src/spinneret/config/config_service.cpp
    src/spinneret/config/template_service.cpp
    src/spinneret/security/api_key_service.cpp
    src/spinneret/metrics/metrics.cpp
    src/spinneret/registry/adapter_registry.cpp
    src/spinneret/lifecycle/smart_restart.cpp
    src/spinneret/adapters/mq/mq_session.cpp
    src/spinneret/adapters/in/rest_input_adapter.cpp
    src/spinneret/adapters/in/ibmmq_input_adapter.cpp
    src/spinneret/adapters/out/rest_output_adapter.cpp
    src/spinneret/adapters/out/ibmmq_output_adapter.cpp
    src/spinneret/http/http_util.cpp
    src/spinneret/http/admin_routes.cpp
    src/spinneret/http/ops_routes.cpp
    src/spinneret/http/http_server.cpp
    src/spinneret/app/application.cpp
    ${SPINNERET_GENERATED_DIR}/openapi_spec.cpp)

target_include_directories(spinneret_core PUBLIC src ${SPINNERET_GENERATED_DIR})
target_link_libraries(spinneret_core PUBLIC
    pugixml::pugixml
    nlohmann_json::nlohmann_json
    httplib::httplib
    Threads::Threads)

if(WIN32)
    target_compile_definitions(spinneret_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(spinneret_core PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(spinneret_core PRIVATE /W4 /utf-8)
endif()

if(SPINNERET_WITH_IBMMQ)
    find_path(MQ_INCLUDE_DIR cmqc.h
        HINTS ${SPINNERET_MQ_HOME}/inc ${SPINNERET_MQ_HOME}/tools/c/include
        PATHS "C:/Program Files/IBM/MQ/tools/c/include" /opt/mqm/inc)
    find_library(MQ_CLIENT_LIBRARY NAMES mqic_r mqic
        HINTS ${SPINNERET_MQ_HOME}/lib64 ${SPINNERET_MQ_HOME}/tools/lib64
        PATHS "C:/Program Files/IBM/MQ/tools/lib64" /opt/mqm/lib64)
    if(NOT MQ_INCLUDE_DIR OR NOT MQ_CLIENT_LIBRARY)
        message(FATAL_ERROR "SPINNERET_WITH_IBMMQ=ON but the IBM MQ client SDK was not found. "
                            "Install the IBM MQ redistributable client and set SPINNERET_MQ_HOME.")
    endif()
    target_sources(spinneret_core PRIVATE src/spinneret/adapters/mq/mqi_session.cpp)
    target_include_directories(spinneret_core PRIVATE ${MQ_INCLUDE_DIR})
    target_link_libraries(spinneret_core PUBLIC ${MQ_CLIENT_LIBRARY})
    target_compile_definitions(spinneret_core PUBLIC SPINNERET_WITH_IBMMQ=1)
    message(STATUS "IBM MQ support: ON (${MQ_CLIENT_LIBRARY})")
else()
    target_sources(spinneret_core PRIVATE src/spinneret/adapters/mq/no_mq_session.cpp)
    message(STATUS "IBM MQ support: OFF")
endif()

# ── Executable ───────────────────────────────────────────────────────────────

add_executable(spinneret src/main.cpp)
target_link_libraries(spinneret PRIVATE spinneret_core)

# ── Tests ────────────────────────────────────────────────────────────────────

if(SPINNERET_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)

    add_executable(spinneret_tests
        tests/test_support.cpp
        tests/payload_extractor_test.cpp
        tests/header_intent_test.cpp
        tests/pipeline_samples_test.cpp
        tests/pipeline_errors_test.cpp
        tests/config_service_test.cpp
        tests/api_key_service_test.cpp
        tests/util_test.cpp
        tests/mq_input_adapter_test.cpp
        tests/http_api_test.cpp)
    target_link_libraries(spinneret_tests PRIVATE spinneret_core GTest::gtest_main)
    target_compile_definitions(spinneret_tests PRIVATE
        SPINNERET_SAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/samples"
        SPINNERET_TEMPLATES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/config/templates")

    include(GoogleTest)
    gtest_discover_tests(spinneret_tests DISCOVERY_MODE PRE_TEST)
endif()
