cmake_minimum_required(VERSION 3.20.3)
project(oq2 VERSION 1.0.0 LANGUAGES CXX)

# initialize compile options
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(COMPILE_OPTIONS
            -g
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

# bison/flex testing executable:
add_executable(oq2bf main/oq2.cpp ${BINARY_GEN_FILES})

# need to add flex headers manually on macos
if (UNIX AND APPLE)
    target_include_directories(oq2bf PRIVATE "/opt/homebrew/opt/flex/include")
endif()

target_include_directories(oq2bf PRIVATE "src" ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(oq2bf PRIVATE ${COMPILE_OPTIONS})
target_compile_definitions(oq2bf PRIVATE -DQELIB1_INC_PATH="${CMAKE_CURRENT_SOURCE_DIR}/qelib1.inc")