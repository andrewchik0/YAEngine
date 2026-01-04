function(add_shader_compilation TARGET_NAME SHADER_SOURCE_DIR)
  set(SHADER_BIN_DIR
    ${CMAKE_BINARY_DIR}/Shaders/${TARGET_NAME}
  )

  add_custom_target(${TARGET_NAME}_Shaders
    COMMAND CompileShaders
    --glslc ${GLSLC_EXECUTABLE}
    --source ${SHADER_SOURCE_DIR}
    --output ${SHADER_BIN_DIR}
    DEPENDS CompileShaders
    COMMENT "Compiling shaders for ${TARGET_NAME}"
  )

  add_dependencies(${TARGET_NAME} ${TARGET_NAME}_Shaders)

  target_compile_definitions(${TARGET_NAME} PRIVATE
    SHADER_BIN_DIR="${SHADER_BIN_DIR}"
  )
endfunction()
