cmake_minimum_required(VERSION 3.20.3)

find_package(HDF5 REQUIRED COMPONENTS CXX)
find_package(CURL REQUIRED)

###################################################
###################################################

add_executable(bisquit
    src/main.cpp
    src/hamiltonian.cpp
    src/circuit_utils.cpp)

target_include_directories(bisquit PRIVATE
    "src"
    "${CMAKE_SOURCE_DIR}/deps")

target_compile_options(bisquit PRIVATE ${COMPILE_OPTIONS})

target_link_libraries(bisquit PRIVATE
    HDF5::HDF5
    CURL::libcurl)

###################################################
###################################################
