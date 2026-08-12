cmake_minimum_required(VERSION 3.16)
project(quant_engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(MSVC)
    add_compile_options(/FS /utf-8)
endif()
if(WIN32)
    add_compile_definitions(WIN32_LEAN_AND_MEAN _UNICODE UNICODE NOMINMAX)
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vendor/graphflow-cpp/CMakeLists.txt")
    message(FATAL_ERROR
        "graphflow-cpp submodule is missing. Run: git submodule update --init --recursive")
endif()
set(GRAPHFLOW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GRAPHFLOW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/graphflow-cpp)

add_library(quant_engine STATIC
    src/quant_config.cpp
    src/quant_dependencies.cpp
    src/quant_engine.cpp
    src/quant_workflow.cpp
    src/runtime/quant_runtime_context.cpp
    src/nodes/quant_nodes.cpp
    src/nodes/node_creators.cpp
)
target_include_directories(quant_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(quant_engine PUBLIC graphflow_engine nlohmann_json)
add_library(quant-engine::quant-engine ALIAS quant_engine)

add_library(quant_engine_api STATIC api/src/quant_trading_api.cpp)
target_include_directories(quant_engine_api PUBLIC api/include)
target_link_libraries(quant_engine_api PUBLIC quant_engine nlohmann_json)
add_library(quant-engine::api ALIAS quant_engine_api)

option(QUANT_ENGINE_BUILD_CLI "Build the quant engine CLI" ON)
if(QUANT_ENGINE_BUILD_CLI)
    add_executable(quant_engine_cli cli/main.cpp)
    target_link_libraries(quant_engine_cli PRIVATE quant_engine_api nlohmann_json)
    target_compile_definitions(quant_engine_cli PRIVATE
        GRAPHFLOW_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
endif()

option(QUANT_ENGINE_BUILD_TESTS "Build Quant Engine tests" OFF)
if(QUANT_ENGINE_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
    add_subdirectory(tests)
endif()
