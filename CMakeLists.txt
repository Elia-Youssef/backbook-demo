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

add_library(backbook-domain STATIC
    src/domain/date.cpp
    src/domain/domain.cpp
    src/domain/fx_terms.cpp
    src/domain/ledger.cpp
    src/domain/ledger_totals.cpp
    src/domain/lifecycle.cpp
    src/domain/money.cpp
    src/domain/posting_policy.cpp
    src/domain/trade.cpp
)
add_library(backbook::domain ALIAS backbook-domain)

target_include_directories(backbook-domain
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_compile_features(backbook-domain PUBLIC cxx_std_20)
backbook_enable_warnings(backbook-domain)

include(CTest)

if(BUILD_TESTING)
    include(FetchContent)

    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723
        GIT_SHALLOW FALSE
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(backbook-domain-tests
        tests/unit/date_tests.cpp
        tests/unit/domain_scenario_tests.cpp
        tests/unit/domain_smoke_tests.cpp
        tests/unit/fx_terms_tests.cpp
        tests/unit/id_tests.cpp
        tests/unit/ledger_tests.cpp
        tests/unit/ledger_totals_tests.cpp
        tests/unit/lifecycle_tests.cpp
        tests/unit/money_tests.cpp
        tests/unit/outcome_tests.cpp
        tests/unit/posting_policy_tests.cpp
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
endif()
