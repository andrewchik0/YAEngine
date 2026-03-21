include(dependencies.cmake)
include(sources.cmake)

add_library(Core STATIC)
target_sources(Core PRIVATE ${SOURCE_FILES})

set_target_properties(Core PROPERTIES LINKER_LANGUAGE CXX)

# Dependencies
target_include_directories(Core PUBLIC
  ${glm_SOURCE_DIR}
  ${Vulkan_INCLUDE_DIRS}
  Vendor
  Source
)
target_link_libraries(Core PUBLIC
  imgui
  glfw
  assimp
)
find_package(Vulkan REQUIRED)
target_link_libraries(Core PUBLIC Vulkan::Vulkan EnTT::EnTT)

# Precompiled header
target_precompile_headers(Core PRIVATE Source/Pch.h)

# Compile shaders
include(../Tools/CompileShaders/add_shader_compilation.cmake)
add_shader_compilation(Core ${CMAKE_CURRENT_SOURCE_DIR}/Shaders)
target_compile_definitions(Core PRIVATE
  SHADER_BIN_DIR="${SHADER_BIN_DIR}"
)
target_compile_definitions(Core PRIVATE
  WORKING_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# Match VS filters to folder tree
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCE_FILES})
# Set working directory for VS debugger
set_target_properties(Core PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
