cmake_minimum_required(VERSION 3.24)

project(
    backbook
    VERSION 0.1.0
    DESCRIPTION "Deterministic C++ domain model for an FX back-office demo"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/CompilerWarnings.cmake)
include(FetchContent)

set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_TEST OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    cpp_httplib
    URL
        https://github.com/yhirose/cpp-httplib/archive/d66d9a95997d51a8ba9822a611d1267757741535.tar.gz
    URL_HASH
        SHA256=5a01b9431d83a521d715b7adebe39db47b1dbde9f6d6c1284d66ff5ea8a031b1
)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    URL
        https://github.com/nlohmann/json/archive/65ee68451d8eb2b5f3a30b410476ab83deb3289b.tar.gz
    URL_HASH
        SHA256=13ef31d691947940a08909f8e0772f1d7d68e5da1678ee812a49c4bb0c996b2f
)

FetchContent_MakeAvailable(cpp_httplib nlohmann_json)

add_library(backbook-domain STATIC
    src/domain/business_calendar.cpp
    src/domain/date.cpp
    src/domain/domain.cpp
    src/domain/fx_terms.cpp
    src/domain/ledger.cpp
    src/domain/ledger_totals.cpp
    src/domain/lifecycle.cpp
    src/domain/limits.cpp
    src/domain/money.cpp
    src/domain/posting_policy.cpp
    src/domain/settlement.cpp
    src/domain/state.cpp
    src/domain/trade.cpp
)
add_library(backbook::domain ALIAS backbook-domain)

target_include_directories(backbook-domain
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(backbook-domain PUBLIC cxx_std_20)
backbook_enable_warnings(backbook-domain)

add_library(backbook-journal-codec STATIC
    src/journal/codec.cpp
    src/journal/command_batch.cpp
    src/journal/fingerprint.cpp
    src/journal/replay.cpp
)
add_library(backbook::journal-codec ALIAS backbook-journal-codec)

target_include_directories(backbook-journal-codec
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(backbook-journal-codec PUBLIC backbook::domain)
target_compile_features(backbook-journal-codec PUBLIC cxx_std_20)
backbook_enable_warnings(backbook-journal-codec)

add_library(backbook-storage STATIC
    src/storage/journal_store.cpp
)
add_library(backbook::storage ALIAS backbook-storage)

target_include_directories(backbook-storage
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    backbook-storage
    PUBLIC
        backbook::journal-codec
)
target_compile_features(backbook-storage PUBLIC cxx_std_20)
backbook_enable_warnings(backbook-storage)

add_library(backbook-service STATIC
    src/service/command.cpp
    src/service/command_evaluator.cpp
    src/service/command_service.cpp
)
add_library(backbook::service ALIAS backbook-service)

target_include_directories(backbook-service
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    backbook-service
    PUBLIC
        backbook::storage
)
target_compile_features(backbook-service PUBLIC cxx_std_20)
backbook_enable_warnings(backbook-service)

add_library(backbook-server-core STATIC
    src/server/demo.cpp
    src/server/http_server.cpp
    src/server/json_codec.cpp
)
add_library(backbook::server-core ALIAS backbook-server-core)

target_include_directories(backbook-server-core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(
    backbook-server-core
    PUBLIC
        backbook::service
    PRIVATE
        httplib::httplib
        nlohmann_json::nlohmann_json
)
target_compile_features(backbook-server-core PUBLIC cxx_std_20)
backbook_enable_warnings(backbook-server-core)

add_executable(backbook-server
    src/server/main.cpp
)
target_link_libraries(
    backbook-server
    PRIVATE
        backbook::server-core
)
target_compile_features(backbook-server PRIVATE cxx_std_20)
backbook_enable_warnings(backbook-server)

include(CTest)

if(BUILD_TESTING)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(backbook-domain-tests
        tests/unit/business_calendar_tests.cpp
        tests/unit/date_tests.cpp
        tests/unit/domain_scenario_tests.cpp
        tests/unit/domain_smoke_tests.cpp
        tests/unit/fx_terms_tests.cpp
        tests/unit/id_tests.cpp
        tests/unit/ledger_tests.cpp
        tests/unit/ledger_totals_tests.cpp
        tests/unit/lifecycle_tests.cpp
        tests/unit/limits_tests.cpp
        tests/unit/money_tests.cpp
        tests/unit/outcome_tests.cpp
        tests/unit/posting_policy_tests.cpp
        tests/unit/settlement_tests.cpp
        tests/unit/state_tests.cpp
        tests/unit/trade_tests.cpp
    )
    target_link_libraries(backbook-domain-tests
        PRIVATE
            backbook::domain
            GTest::gtest_main
    )
    backbook_enable_warnings(backbook-domain-tests)

    include(GoogleTest)
    gtest_discover_tests(
        backbook-domain-tests
        DISCOVERY_TIMEOUT 30
        PROPERTIES TIMEOUT 30
    )

    add_executable(backbook-journal-tests
        tests/unit/journal_codec_tests.cpp
        tests/unit/journal_replay_tests.cpp
        tests/unit/journal_storage_tests.cpp
    )
    target_include_directories(
        backbook-journal-tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    target_link_libraries(
        backbook-journal-tests
        PRIVATE
            backbook::storage
            GTest::gtest_main
    )
    backbook_enable_warnings(backbook-journal-tests)

    gtest_discover_tests(
        backbook-journal-tests
        DISCOVERY_TIMEOUT 30
        PROPERTIES TIMEOUT 30
    )

    add_executable(backbook-service-tests
        tests/unit/command_service_tests.cpp
    )
    target_include_directories(
        backbook-service-tests
        PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/tests"
    )
    target_link_libraries(
        backbook-service-tests
        PRIVATE
            backbook::service
            GTest::gtest_main
    )
    backbook_enable_warnings(backbook-service-tests)

    gtest_discover_tests(
        backbook-service-tests
        DISCOVERY_TIMEOUT 30
        PROPERTIES TIMEOUT 30
    )

    add_executable(backbook-http-tests
        tests/http/demo_tests.cpp
        tests/http/http_server_tests.cpp
    )
    target_link_libraries(
        backbook-http-tests
        PRIVATE
            backbook::server-core
            httplib::httplib
            nlohmann_json::nlohmann_json
            GTest::gtest_main
    )
    backbook_enable_warnings(backbook-http-tests)

    gtest_discover_tests(
        backbook-http-tests
        DISCOVERY_TIMEOUT 30
        PROPERTIES
            TIMEOUT 30
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endif()
