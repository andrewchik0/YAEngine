#include "Editor/ShaderCompiler.h"
#include "Utils/Log.h"

#include <windows.h>

namespace YAEngine
{
  void ShaderCompiler::Init(const std::string& shaderSourceDir, const std::string& shaderBinDir,
                            const std::string& compilerExePath, const std::string& glslcPath)
  {
    m_ShaderSourceDir = shaderSourceDir;
    m_ShaderBinDir = shaderBinDir;
    m_CompilerExePath = compilerExePath;
    m_GlslcPath = glslcPath;
  }

  CompileResult ShaderCompiler::Compile(const std::string& inputFile, const std::string& outputFile,
                                        const std::vector<std::string>& defines) const
  {
    std::string inputPath = m_ShaderSourceDir + "/" + inputFile;
    std::string outputPath = m_ShaderBinDir + "/" + outputFile;

    std::string cmdLine = "\"" + m_CompilerExePath + "\""
      + " --single"
      + " --glslc \"" + m_GlslcPath + "\""
      + " --input \"" + inputPath + "\""
      + " --output \"" + outputPath + "\"";

    for (auto& def : defines)
      cmdLine += " -D" + def;

    SECURITY_ATTRIBUTES sa {};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hReadPipe = nullptr;
    HANDLE hWritePipe = nullptr;
    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
      return { false, "Failed to create pipe for shader compilation" };

    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdError = hWritePipe;
    si.hStdOutput = hWritePipe;

    PROCESS_INFORMATION pi {};

    BOOL created = CreateProcessA(
      nullptr,
      cmdLine.data(),
      nullptr, nullptr,
      TRUE,
      CREATE_NO_WINDOW,
      nullptr, nullptr,
      &si, &pi);

    CloseHandle(hWritePipe);

    if (!created)
    {
      CloseHandle(hReadPipe);
      return { false, "Failed to launch CompileShaders process" };
    }

    std::string output;
    char buffer[512];
    DWORD bytesRead = 0;
    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0)
    {
      buffer[bytesRead] = '\0';
      output += buffer;
    }
    CloseHandle(hReadPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    if (exitCode != 0)
      return { false, output.empty() ? "Shader compilation failed (exit code " + std::to_string(exitCode) + ")" : output };

    return { true, {} };
  }
}
