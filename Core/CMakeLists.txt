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
  Shared
)
target_link_libraries(Core PUBLIC
  imgui
  glfw
  assimp
  yaml-cpp::yaml-cpp
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

# mesh.vert permutations (default permutation compiled by directory mode above)
add_shader_permutation(Core ${CMAKE_CURRENT_SOURCE_DIR}/Shaders
  mesh.vert mesh_instanced.vert.spv -DINSTANCED)
add_shader_permutation(Core ${CMAKE_CURRENT_SOURCE_DIR}/Shaders
  mesh.vert mesh_depth.vert.spv -DDEPTH_ONLY)
add_shader_permutation(Core ${CMAKE_CURRENT_SOURCE_DIR}/Shaders
  mesh.vert mesh_instanced_depth.vert.spv -DINSTANCED -DDEPTH_ONLY)
add_shader_permutation(Core ${CMAKE_CURRENT_SOURCE_DIR}/Shaders
  shadow.vert shadow_instanced.vert.spv -DINSTANCED)
add_shader_permutation(Core ${CMAKE_CURRENT_SOURCE_DIR}/Shaders
  shadow.vert shadow_alphatest.vert.spv -DALPHA_TEST)
add_shader_permutation(Core ${CMAKE_CURRENT_SOURCE_DIR}/Shaders
  shadow.vert shadow_instanced_alphatest.vert.spv -DINSTANCED -DALPHA_TEST)
add_shader_permutation(Core ${CMAKE_CURRENT_SOURCE_DIR}/Shaders
  mesh.vert mesh_depth_alphatest.vert.spv -DDEPTH_ONLY -DALPHA_TEST)
add_shader_permutation(Core ${CMAKE_CURRENT_SOURCE_DIR}/Shaders
  mesh.vert mesh_instanced_depth_alphatest.vert.spv -DINSTANCED -DDEPTH_ONLY -DALPHA_TEST)
add_shader_permutation(Core ${CMAKE_CURRENT_SOURCE_DIR}/Shaders
  mesh.vert mesh_transparent_instanced.vert.spv -DINSTANCED -DTRANSPARENT)
add_shader_permutation(Core ${CMAKE_CURRENT_SOURCE_DIR}/Shaders
  gbuffer.frag gbuffer_alphatest.frag.spv -DALPHA_TEST)
target_compile_definitions(Core PRIVATE
  WORKING_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

if(YA_EDITOR)
  target_compile_definitions(Core PUBLIC YA_EDITOR)
  target_compile_definitions(Core PRIVATE
    COMPILE_SHADERS_EXE="$<TARGET_FILE:CompileShaders>"
    SHADER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
    SHADER_SHARED_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Shared"
    GLSLC_PATH="${GLSLC_EXECUTABLE}"
    SHADER_PERMUTATIONS_FILE="${SHADER_PERMUTATIONS_FILE}"
  )
  target_link_libraries(Core PRIVATE nfd)
endif()

# Match VS filters to folder tree
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCE_FILES})
# Set working directory for VS debugger
set_target_properties(Core PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
