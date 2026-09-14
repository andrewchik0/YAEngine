include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# A commit on the docking branch (IMGUI_VERSION_NUM 19297), pinned together with imgui_test_engine
# below, which tracks ImGui internals. Bump both at once. A commit cannot be cloned shallow.
FetchContent_Declare(ImGui
  GIT_REPOSITORY https://github.com/ocornut/imgui
  GIT_TAG 367b2c24f399988ddafc0bb4628da0106bcc09be
  EXCLUDE_FROM_ALL
  SYSTEM)
FetchContent_MakeAvailable(ImGui)
FetchContent_GetProperties(ImGui SOURCE_DIR IMGUI_DIR)

FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        1.0.3
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

FetchContent_Declare(
  nativefiledialog-extended
  GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
  GIT_TAG v1.3.0
)
FetchContent_MakeAvailable(nativefiledialog-extended)

FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG yaml-cpp-0.9.0
)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yaml-cpp)

set(MESHOPT_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(MESHOPT_BUILD_GLTFPACK OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  meshoptimizer
  GIT_REPOSITORY https://github.com/zeux/meshoptimizer.git
  GIT_TAG        v1.2
)
FetchContent_MakeAvailable(meshoptimizer)

set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  assimp
  GIT_REPOSITORY https://github.com/assimp/assimp.git
  GIT_TAG        v6.0.5
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
# ImPlot is only used by the editor performance panel, so non-editor builds
# neither fetch nor compile it.
if(YA_EDITOR)
  FetchContent_Declare(implot
    GIT_REPOSITORY https://github.com/epezent/implot
    GIT_TAG v1.0
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
    SYSTEM)
  FetchContent_MakeAvailable(implot)

  # implot_demo.cpp is not built: it is a large translation unit we never call into.
  add_library(implot STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
  )
  target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
  target_link_libraries(implot PUBLIC imgui)

  # The agent bridge speaks JSON lines and only exists in editor builds.
  FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json
    GIT_TAG v3.12.0
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
    SYSTEM)
  FetchContent_MakeAvailable(nlohmann_json)

  # Dear ImGui Test Engine drives the editor UI for the agent bridge (ui.* methods). Its own
  # license (imgui_test_engine/LICENSE.txt) applies, free for individuals and open source.
  # Pinned to a commit that matches the ImGui commit above: it tracks ImGui internals. The
  # repository has no CMakeLists.txt.
  FetchContent_Declare(imgui_test_engine
    GIT_REPOSITORY https://github.com/ocornut/imgui_test_engine
    GIT_TAG cf4b9749fad4cfdf2096a27f9a688ea71c47fe49
    EXCLUDE_FROM_ALL
    SYSTEM)
  FetchContent_MakeAvailable(imgui_test_engine)

  # Compiled into imgui itself: imgui.cpp calls the hooks the test engine defines, and every
  # translation unit that sees imgui_internal.h has to agree on IMGUI_ENABLE_TEST_ENGINE.
  set(IMGUI_TEST_ENGINE_DIR ${imgui_test_engine_SOURCE_DIR}/imgui_test_engine)
  target_sources(imgui PRIVATE
    ${IMGUI_TEST_ENGINE_DIR}/imgui_te_engine.cpp
    ${IMGUI_TEST_ENGINE_DIR}/imgui_te_context.cpp
    ${IMGUI_TEST_ENGINE_DIR}/imgui_te_coroutine.cpp
    ${IMGUI_TEST_ENGINE_DIR}/imgui_te_utils.cpp
    ${IMGUI_TEST_ENGINE_DIR}/imgui_te_exporters.cpp
    ${IMGUI_TEST_ENGINE_DIR}/imgui_te_perftool.cpp
    ${IMGUI_TEST_ENGINE_DIR}/imgui_te_ui.cpp
    ${IMGUI_TEST_ENGINE_DIR}/imgui_capture_tool.cpp
  )
  target_include_directories(imgui PUBLIC ${imgui_test_engine_SOURCE_DIR})
  # Screenshots come from the swapchain readback, so the capture tool and its stb copy stay out.
  target_compile_definitions(imgui PUBLIC
    IMGUI_ENABLE_TEST_ENGINE
    IMGUI_TEST_ENGINE_ENABLE_COROUTINE_STDTHREAD_IMPL=1
    IMGUI_TEST_ENGINE_ENABLE_CAPTURE=0
    IMGUI_TEST_ENGINE_ENABLE_IMPLOT=0
  )
endif()
