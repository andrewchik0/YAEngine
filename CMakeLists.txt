cmake_minimum_required(VERSION 3.10)
project(YAEngine)

set(CMAKE_CXX_STANDARD 23)

# NVIDIA Streamline (DLSS) is Windows/x64 only, everywhere else the vendored SDK is unusable.
if(WIN32)
  option(YA_DLSS "Enable NVIDIA DLSS through the vendored Streamline SDK" ON)
else()
  option(YA_DLSS "Enable NVIDIA DLSS through the vendored Streamline SDK" OFF)
endif()

set(STREAMLINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Core/Vendor/Streamline")
set(STREAMLINE_RUNTIME_DLLS
  "${STREAMLINE_DIR}/bin/x64/sl.interposer.dll"
  "${STREAMLINE_DIR}/bin/x64/sl.common.dll"
  "${STREAMLINE_DIR}/bin/x64/sl.dlss.dll"
  "${STREAMLINE_DIR}/bin/x64/nvngx_dlss.dll"
)

find_program(GLSLC_EXECUTABLE
  NAMES glslc glslc.exe
  HINTS
    ENV VULKAN_SDK
  PATH_SUFFIXES Bin bin
  REQUIRED
)

add_subdirectory(Core)
add_subdirectory(RacingDemo)
add_subdirectory(Tools/CompileShaders)
