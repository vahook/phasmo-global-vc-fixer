cmake_minimum_required(VERSION 3.10)
project(phasmo_global_vc_fixer)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message( FATAL_ERROR "Expected a 64-bit target platform." )
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(phasmo_global_vc_fixer src/main.cpp src/rpm.cpp src/mmap_view.cpp src/phasmem.cpp src/il2cpp_rpm.cpp)
if (WIN32)
  target_compile_definitions(phasmo_global_vc_fixer PUBLIC UNICODE _UNICODE)
endif()