cmake_minimum_required(VERSION 3.16)

project(langton_ant_mpi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(MPI REQUIRED)

add_executable(langton_ant
    src/main.cpp
    src/simulation.cpp
    src/mpi_simulation.cpp
    src/ppm.cpp
)

target_include_directories(langton_ant PRIVATE src)
target_link_libraries(langton_ant PRIVATE MPI::MPI_CXX)

if(MSVC)
    target_compile_options(langton_ant PRIVATE /W4)
else()
    target_compile_options(langton_ant PRIVATE -Wall -Wextra -pedantic)
endif()
