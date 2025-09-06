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

# compile bison/flex files:
flex_target(OQ2Lexer 
            src/oq2/lexer.l 
            ${CMAKE_CURRENT_BINARY_DIR}/lexer.cpp)
bison_target(OQ2Parser 
            src/oq2/parser.y 
            ${CMAKE_CURRENT_BINARY_DIR}/parser.cpp 
            DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/parser.tab.h)

add_flex_bison_dependency(OQ2Lexer OQ2Parser)

###################################################
###################################################

message(STATUS "BISON_OQ2Parser_OUTPUTS: ${BISON_OQ2Parser_OUTPUTS}")

set(BINARY_GEN_FILES
    ${BISON_OQ2Parser_OUTPUTS} 
    ${FLEX_OQ2Lexer_OUTPUTS}
    src/instruction.cpp
    src/program.cpp
    src/program/expression.cpp)

set(SIM_FILES
    src/instruction.cpp
    src/sim/client.cpp
    src/sim/clock.cpp
    src/sim/compute.cpp
    src/sim/compute/execute.cpp
    src/sim/compute/init.cpp
    src/sim/compute/replacement.cpp
    src/sim/compute/replacement/lru.cpp
    src/sim/compute/replacement/lti.cpp
    src/sim/magic_state.cpp
    src/sim/memory.cpp
    src/sim/routing.cpp)

# binary generation executable:
add_executable(qs_gen_binary main/qs_gen_binary.cpp ${BINARY_GEN_FILES})

# need to add flex headers manually on macos
if (UNIX AND APPLE)
    target_include_directories(qs_gen_binary PRIVATE "/opt/homebrew/opt/flex/include")
endif()

target_include_directories(qs_gen_binary PRIVATE "src" ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(qs_gen_binary PRIVATE ${COMPILE_OPTIONS})
target_compile_definitions(qs_gen_binary PRIVATE -DQELIB1_INC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/qelib1.inc")

target_link_libraries(qs_gen_binary PRIVATE ZLIB::ZLIB)

# simulator:
add_executable(qs_sim main/qs_sim.cpp ${SIM_FILES})

target_include_directories(qs_sim PRIVATE "src")
target_compile_options(qs_sim PRIVATE ${COMPILE_OPTIONS})
target_compile_definitions(qs_sim PRIVATE -DQELIB1_INC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/qelib1.inc")

target_link_libraries(qs_sim PRIVATE ZLIB::ZLIB)