cmake_minimum_required(VERSION 3.10)
project(YAEngine)

set(CMAKE_CXX_STANDARD 23)

find_program(GLSLC_EXECUTABLE
  NAMES glslc glslc.exe
  HINTS
    ENV VULKAN_SDK
  PATH_SUFFIXES Bin bin
  REQUIRED
)

add_subdirectory(Core)
add_subdirectory(App)
add_subdirectory(Tools/CompileShaders)
