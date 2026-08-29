set(SOURCES
  Source/Main.cpp
  Source/ControlsLayer.h
  Source/ControlsLayer.cpp
  Source/AppLayer.h
  Source/GameComponents.h
  Source/GameComponentSerializers.h
  Source/GameComponentSerializers.cpp
  Source/ReelFormat.h
  Source/ReelFormat.cpp
  Source/ReelPlaybackLayer.h
  Source/ReelPlaybackLayer.cpp
)

add_executable(RacingDemo)

target_sources(RacingDemo PRIVATE ${SOURCES})

target_link_libraries(RacingDemo Core)

set(APP_WORKING_DIR "${CMAKE_SOURCE_DIR}")

target_compile_definitions(RacingDemo PRIVATE
  APP_WORKING_DIR="${APP_WORKING_DIR}/RacingDemo"
)

if(YA_DLSS)
  # Streamline looks for its plugins next to the host executable.
  add_custom_command(TARGET RacingDemo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${STREAMLINE_RUNTIME_DLLS}
      "$<TARGET_FILE_DIR:RacingDemo>"
    COMMENT "Copying Streamline runtime next to RacingDemo.exe"
  )
endif()

# Match VS filters to directory structure on disk
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${SOURCES})
# Set working directory for VS debugger
set_target_properties(RacingDemo PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/RacingDemo"
)