include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(ImGui
  GIT_REPOSITORY https://github.com/ocornut/imgui
  GIT_TAG docking
  GIT_SHALLOW ON
  EXCLUDE_FROM_ALL
  SYSTEM)
FetchContent_MakeAvailable(ImGui)
FetchContent_GetProperties(ImGui SOURCE_DIR IMGUI_DIR)

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        1.0.1
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
  entt
  GIT_REPOSITORY https://github.com/skypjack/entt.git
  GIT_TAG v3.16.0
)
FetchContent_MakeAvailable(entt)

set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  assimp
  GIT_REPOSITORY https://github.com/assimp/assimp.git
  GIT_TAG        v5.4.3
)
FetchContent_MakeAvailable(assimp)

add_library(imgui STATIC
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_demo.cpp
)
target_include_directories(imgui PUBLIC
  ${glfw_SOURCE_DIR}/include
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
)