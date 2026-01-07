set(SOURCES
  Source/Main.cpp
  Source/ControlsLayer.h
  Source/ControlsLayer.cpp
  Source/AppLayer.h
)

add_executable(RacingDemo)

target_sources(RacingDemo PRIVATE ${SOURCES})

target_link_libraries(RacingDemo Core)

set(APP_WORKING_DIR "${CMAKE_SOURCE_DIR}")

target_compile_definitions(RacingDemo PRIVATE
  APP_WORKING_DIR="${APP_WORKING_DIR}/RacingDemo"
)

# Match VS filters to directory structure on disk
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCES})
# Set working directory for VS debugger
set_target_properties(RacingDemo PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/RacingDemo"
)