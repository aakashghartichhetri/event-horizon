cmake_minimum_required(VERSION 3.23)
project(event_horizon VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(EH_BUILD_SERVER "Build the local HTTP API and dashboard" ON)
option(EH_BUILD_TESTS "Build the dependency-free C++ test suite" ON)
option(EH_WITH_GTEST "Build optional GoogleTest integration (network required)" OFF)
option(EH_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)

if(EH_ENABLE_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Sanitizers require GCC or Clang")
    endif()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_library(event_horizon_lib
    src/exchange.cpp src/replay.cpp src/report.cpp)
target_include_directories(event_horizon_lib PUBLIC ${PROJECT_SOURCE_DIR}/include)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(event_horizon_lib PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

add_executable(event_horizon_cli src/main.cpp)
set_target_properties(event_horizon_cli PROPERTIES OUTPUT_NAME event-horizon)
target_link_libraries(event_horizon_cli PRIVATE event_horizon_lib)

if(EH_BUILD_SERVER)
    # Crow depends on Asio; for offline-reproducible builds this implementation
    # uses the packaged, battle-tested Boost.Beast HTTP layer (Boost 1.83).
    if(POLICY CMP0167)
        cmake_policy(SET CMP0167 NEW)
    endif()
    find_package(Boost 1.83 REQUIRED)
    find_package(Threads REQUIRED)
    add_executable(event_horizon_server src/server.cpp)
    set_target_properties(event_horizon_server PROPERTIES OUTPUT_NAME event-horizon-server)
    target_include_directories(event_horizon_server PRIVATE ${Boost_INCLUDE_DIRS})
    target_compile_definitions(event_horizon_server PRIVATE BOOST_ERROR_CODE_HEADER_ONLY)
    target_link_libraries(event_horizon_server PRIVATE event_horizon_lib Threads::Threads)
endif()

if(EH_BUILD_TESTS OR EH_WITH_GTEST)
    enable_testing()
endif()

if(EH_BUILD_TESTS)
    add_executable(event_horizon_tests tests/core_tests.cpp tests/system_tests.cpp)
    target_link_libraries(event_horizon_tests PRIVATE event_horizon_lib)
    add_test(NAME core_tests COMMAND event_horizon_tests)
    set_tests_properties(core_tests PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    add_test(NAME demo_smoke COMMAND event_horizon_cli demo)
    set_tests_properties(demo_smoke PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    add_test(NAME replay_smoke COMMAND event_horizon_cli replay data/baseline.csv)
    set_tests_properties(replay_smoke PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    add_test(NAME benchmark_smoke COMMAND event_horizon_cli benchmark 100)
    set_tests_properties(benchmark_smoke PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    add_test(NAME scenarios_json_smoke COMMAND event_horizon_cli compare-json)
    set_tests_properties(scenarios_json_smoke PROPERTIES WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
    if(EH_BUILD_SERVER)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        add_test(NAME http_integration COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/http_integration.py
            $<TARGET_FILE:event_horizon_server> ${PROJECT_SOURCE_DIR})
    endif()
endif()

if(EH_WITH_GTEST)
    include(FetchContent)
    # GoogleTest 1.17.0: immutable git commit. Optional so offline builds work.
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG 52eb8108c5bdec04579160ae17225d66034bd723)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    add_executable(event_horizon_gtests tests/gtest_tests.cpp)
    target_link_libraries(event_horizon_gtests PRIVATE event_horizon_lib GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(event_horizon_gtests WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endif()
