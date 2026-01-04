set(SOURCES
  Source/Main.cpp
)

add_executable(App)

target_sources(App PRIVATE ${SOURCES})

target_link_libraries(App Core)

set(APP_WORKING_DIR "${CMAKE_SOURCE_DIR}")

target_compile_definitions(App PRIVATE
  APP_WORKING_DIR="${APP_WORKING_DIR}/App"
)

# Match VS filters to directory structure on disk
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCES})
# Set working directory for VS debugger
set_target_properties(App PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/App"
)