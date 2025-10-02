cmake_minimum_required(VERSION 3.20.3)
project(oq2 VERSION 1.0.0 LANGUAGES CXX)

# initialize compile options
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(COMPILE_OPTIONS
            -ggdb3
            -Wall
            -Wextra
            -Wpedantic
            -Wunused
            )
else()
    set(COMPILE_OPTIONS
            -O3
            -flto=auto
            -fstrict-aliasing
            -march=native
            -Wunused
            )
endif()

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(FLEX REQUIRED)
find_package(BISON REQUIRED)
find_package(ZLIB REQUIRED)
find_package(LibLZMA REQUIRED)

# compile bison/flex files:
flex_target(OQ2Lexer 
            src/compiler/program/oq2/lexer.l 
            ${CMAKE_CURRENT_BINARY_DIR}/lexer.cpp)
bison_target(OQ2Parser 
            src/compiler/program/oq2/parser.y 
            ${CMAKE_CURRENT_BINARY_DIR}/parser.cpp 
            DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/parser.tab.h)

add_flex_bison_dependency(OQ2Lexer OQ2Parser)

###################################################
###################################################

message(STATUS "BISON_OQ2Parser_OUTPUTS: ${BISON_OQ2Parser_OUTPUTS}")

set(BINARY_GEN_FILES
    ${BISON_OQ2Parser_OUTPUTS} 
    ${FLEX_OQ2Lexer_OUTPUTS}
    src/generic_io.cpp
    src/instruction.cpp
    src/compiler/program.cpp
    src/compiler/program/expression.cpp)

set(SIM_FILES
    src/argparse.cpp
    src/generic_io.cpp
    src/instruction.cpp
    src/sim.cpp
    src/sim/client.cpp
    src/sim/clock.cpp
    src/sim/cmp/replacement.cpp
    src/sim/cmp/replacement/lru.cpp
    src/sim/cmp/replacement/lti.cpp
    src/sim/compute.cpp
    src/sim/factory.cpp
    src/sim/memory.cpp
    src/sim/routing.cpp)

set(MEMOPT_FILES
    src/compiler/memopt.cpp
    src/compiler/memopt/impl.cpp
    src/compiler/memopt/impl/viszlai.cpp
    src/compiler/memopt/impl/cost_aware.cpp)

###################################################
###################################################

# Quicksilver simulator library:

add_library(qssl SHARED ${SIM_FILES})
target_include_directories(qssl PUBLIC "src")
target_compile_options(qssl PUBLIC ${COMPILE_OPTIONS})
target_compile_definitions(qssl PUBLIC -DQELIB1_INC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/qelib1.inc")
target_link_libraries(qssl PUBLIC ZLIB::ZLIB LibLZMA::LibLZMA)

###################################################
###################################################

# Binary generation executable:

add_executable(qs_gen_binary main/qs_gen_binary.cpp ${BINARY_GEN_FILES})

# need to add flex headers manually on macos
if (UNIX AND APPLE)
    target_include_directories(qs_gen_binary PRIVATE "/opt/homebrew/opt/flex/include")
endif()

target_include_directories(qs_gen_binary PRIVATE "src" ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(qs_gen_binary PRIVATE ${COMPILE_OPTIONS})
target_compile_definitions(qs_gen_binary PRIVATE -DQELIB1_INC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/qelib1.inc")

target_link_libraries(qs_gen_binary PRIVATE ZLIB::ZLIB LibLZMA::LibLZMA)

###################################################
###################################################

# compiler for adding memory instructions:

add_executable(qs_mem_compile main/qs_mem_compile.cpp
                src/argparse.cpp
                src/generic_io.cpp
                src/instruction.cpp
                ${MEMOPT_FILES})
target_compile_options(qs_mem_compile PRIVATE ${COMPILE_OPTIONS})
target_include_directories(qs_mem_compile PRIVATE "src")
target_link_libraries(qs_mem_compile PRIVATE ZLIB::ZLIB LibLZMA::LibLZMA)

###################################################
###################################################

# iterative simulator for memory evaluations:
add_executable(qs_sim_mem main/qs_sim_mem_search.cpp ${MEMOPT_FILES})
target_link_libraries(qs_sim_mem PRIVATE qssl)
target_compile_options(qs_sim_mem PRIVATE ${COMPILE_OPTIONS})
target_include_directories(qs_sim_mem PRIVATE "src")
target_link_libraries(qs_sim_mem PRIVATE ZLIB::ZLIB LibLZMA::LibLZMA)

###################################################
###################################################