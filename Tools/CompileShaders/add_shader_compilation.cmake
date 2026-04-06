function(add_shader_compilation TARGET_NAME SHADER_SOURCE_DIR)
  set(SHADER_BIN_DIR
    ${CMAKE_BINARY_DIR}/Shaders/${TARGET_NAME}
  )

  # Clear permutations CSV (will be populated by add_shader_permutation calls)
  set(SHADER_PERMUTATIONS_FILE ${SHADER_BIN_DIR}/permutations.csv)
  file(WRITE ${SHADER_PERMUTATIONS_FILE} "")
  set(SHADER_PERMUTATIONS_FILE ${SHADER_PERMUTATIONS_FILE} PARENT_SCOPE)

  set(SHADER_OPTIMIZE_FLAG "")
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(SHADER_OPTIMIZE_FLAG "--optimize")
  endif()

  add_custom_target(${TARGET_NAME}_Shaders
    COMMAND CompileShaders
    --glslc ${GLSLC_EXECUTABLE}
    --source ${SHADER_SOURCE_DIR}
    --output ${SHADER_BIN_DIR}
    ${SHADER_OPTIMIZE_FLAG}
    DEPENDS CompileShaders
    COMMENT "Compiling shaders for ${TARGET_NAME}"
  )

  add_dependencies(${TARGET_NAME} ${TARGET_NAME}_Shaders)

  target_compile_definitions(${TARGET_NAME} PRIVATE
    SHADER_BIN_DIR="${SHADER_BIN_DIR}"
  )
endfunction()

# Compile a single shader with preprocessor defines (permutation).
# Usage: add_shader_permutation(TARGET_NAME SOURCE_DIR INPUT_FILE OUTPUT_NAME DEFINES...)
function(add_shader_permutation TARGET_NAME SOURCE_DIR INPUT_FILE OUTPUT_NAME)
  set(SHADER_BIN_DIR ${CMAKE_BINARY_DIR}/Shaders/${TARGET_NAME})
  set(INPUT_PATH ${SOURCE_DIR}/${INPUT_FILE})
  set(OUTPUT_PATH ${SHADER_BIN_DIR}/${OUTPUT_NAME})

  # Remaining args are -D defines
  set(DEFINE_ARGS ${ARGN})

  set(PERM_OPTIMIZE_FLAG "")
  if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(PERM_OPTIMIZE_FLAG "--optimize")
  endif()

  add_custom_command(
    OUTPUT ${OUTPUT_PATH}
    COMMAND CompileShaders
      --single
      --glslc ${GLSLC_EXECUTABLE}
      --input ${INPUT_PATH}
      --output ${OUTPUT_PATH}
      ${PERM_OPTIMIZE_FLAG}
      ${DEFINE_ARGS}
    DEPENDS CompileShaders ${INPUT_PATH}
    COMMENT "Compiling permutation ${OUTPUT_NAME}"
  )

  # Attach to the existing shader target
  if(TARGET ${TARGET_NAME}_Shaders)
    add_custom_target(${TARGET_NAME}_Shader_${OUTPUT_NAME} DEPENDS ${OUTPUT_PATH})
    add_dependencies(${TARGET_NAME}_Shaders ${TARGET_NAME}_Shader_${OUTPUT_NAME})
  endif()

  # Append to permutations CSV: source,output,DEFINE1 DEFINE2 ...
  # Strip -D prefix from each define
  set(CLEAN_DEFINES "")
  foreach(DEF ${DEFINE_ARGS})
    string(REGEX REPLACE "^-D" "" CLEAN_DEF ${DEF})
    if(CLEAN_DEFINES)
      set(CLEAN_DEFINES "${CLEAN_DEFINES} ${CLEAN_DEF}")
    else()
      set(CLEAN_DEFINES "${CLEAN_DEF}")
    endif()
  endforeach()
  if(SHADER_PERMUTATIONS_FILE)
    file(APPEND ${SHADER_PERMUTATIONS_FILE} "${INPUT_FILE},${OUTPUT_NAME},${CLEAN_DEFINES}\n")
  endif()
endfunction()
