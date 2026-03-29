#pragma once

#include "Pch.h"

namespace YAEngine
{
  struct CompileResult
  {
    bool success = false;
    std::string errorMessage;
  };

  class ShaderCompiler
  {
  public:
    void Init(const std::string& shaderSourceDir, const std::string& shaderBinDir,
              const std::string& compilerExePath, const std::string& glslcPath);

    CompileResult Compile(const std::string& inputFile, const std::string& outputFile,
                          const std::vector<std::string>& defines = {}) const;

  private:
    std::string m_ShaderSourceDir;
    std::string m_ShaderBinDir;
    std::string m_CompilerExePath;
    std::string m_GlslcPath;
  };
}
