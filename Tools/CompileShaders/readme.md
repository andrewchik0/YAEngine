# Compile shaders target

### Usage

```cmake
include(.../add_shader_compilation.cmake)
add_shader_compilation(${YourTarget} ${PathToShadersDirectory})
# Add a "#define PATH_TO_SHADERS_BINARY_DIR" to your target
target_compile_definitions(${YourTarget} PRIVATE
  ${PATH_TO_SHADERS_BINARY_DIR}="${SHADER_BIN_DIR}"
)
```
