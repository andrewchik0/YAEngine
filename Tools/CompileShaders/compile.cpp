#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <set>
#include <string>
#include <vector>
#include <algorithm>
#include <windows.h>

namespace fs = std::filesystem;

std::vector<fs::path> CollectShaders(const fs::path& root)
{
  std::vector<fs::path> result;

  for (const auto& entry : fs::recursive_directory_iterator(root))
  {
    if (!entry.is_regular_file())
      continue;

    const auto& p = entry.path();
    auto ext = p.extension().string();

    if (ext == ".vert" || ext == ".frag" || ext == ".comp" || ext == ".geom" || ext == ".tesc" || ext == ".tese")
      result.push_back(p);
  }

  return result;
}

static std::vector<std::string> ReadLines(const fs::path& path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open())
    return {};

  std::string content{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};

  // Strip UTF-8 BOM
  if (content.size() >= 3 &&
      static_cast<uint8_t>(content[0]) == 0xEF &&
      static_cast<uint8_t>(content[1]) == 0xBB &&
      static_cast<uint8_t>(content[2]) == 0xBF)
  {
    content.erase(0, 3);
  }

  std::vector<std::string> lines;
  std::istringstream stream{content};
  std::string line;

  while (std::getline(stream, line))
  {
    // Strip trailing \r
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    lines.push_back(std::move(line));
  }

  return lines;
}

// Trim leading/trailing whitespace from a string
static std::string Trim(const std::string& s)
{
  size_t start = s.find_first_not_of(" \t");
  if (start == std::string::npos)
    return "";
  size_t end = s.find_last_not_of(" \t");
  return s.substr(start, end - start + 1);
}

// Strip trailing line comment (// ...) that is outside of quotes
static std::string StripTrailingComment(const std::string& s)
{
  size_t pos = s.find("//");
  if (pos == std::string::npos)
    return s;
  return s.substr(0, pos);
}

// Parse a preprocessor directive from a line.
// Returns the directive name (e.g. "ifdef", "include", "endif") and the rest of the line after the directive.
// Returns empty directive if the line is not a preprocessor directive.
struct Directive
{
  std::string name;
  std::string args;
};

static Directive ParseDirective(const std::string& line)
{
  std::string trimmed = Trim(line);

  if (trimmed.empty() || trimmed[0] != '#')
    return {};

  // Skip '#' and any whitespace after it (handles "# ifdef" style)
  size_t pos = 1;
  while (pos < trimmed.size() && (trimmed[pos] == ' ' || trimmed[pos] == '\t'))
    ++pos;

  // Extract directive name
  size_t nameStart = pos;
  while (pos < trimmed.size() && trimmed[pos] != ' ' && trimmed[pos] != '\t' && trimmed[pos] != '\n')
    ++pos;

  std::string name = trimmed.substr(nameStart, pos - nameStart);

  // Skip whitespace after directive name
  while (pos < trimmed.size() && (trimmed[pos] == ' ' || trimmed[pos] == '\t'))
    ++pos;

  std::string args = trimmed.substr(pos);

  return {name, args};
}

// Check if a directive+args represents a __cplusplus guard
static bool IsCplusplusGuard(const Directive& d)
{
  if (d.name == "ifdef")
  {
    std::string arg = Trim(StripTrailingComment(d.args));
    return arg == "__cplusplus";
  }

  if (d.name == "if")
  {
    std::string arg = Trim(StripTrailingComment(d.args));
    return arg == "defined(__cplusplus)" || arg == "defined( __cplusplus )";
  }

  return false;
}

enum class ParseState
{
  NORMAL,
  SKIPPING
};

bool ParseShaderFromFile(const fs::path& filePath, std::set<std::string>& includedFiles, std::string& out)
{
  fs::path canonicalPath;
  try
  {
    canonicalPath = fs::weakly_canonical(filePath);
  }
  catch (const std::exception&)
  {
    canonicalPath = filePath;
  }

  auto lines = ReadLines(canonicalPath);
  if (lines.empty() && !fs::exists(canonicalPath))
  {
    std::cerr << "Failed to read shader file: " << canonicalPath.string() << std::endl;
    return false;
  }

  fs::path parentDir = canonicalPath.parent_path();

  ParseState state = ParseState::NORMAL;
  int skipDepth = 0; // nesting depth inside __cplusplus block

  for (size_t i = 0; i < lines.size(); ++i)
  {
    const std::string& line = lines[i];
    Directive dir = ParseDirective(line);

    if (state == ParseState::SKIPPING)
    {
      // Track nesting: #if, #ifdef, #ifndef increase depth
      if (dir.name == "if" || dir.name == "ifdef" || dir.name == "ifndef")
      {
        ++skipDepth;
      }
      else if (dir.name == "endif")
      {
        if (skipDepth == 0)
        {
          // End of the __cplusplus block, return to NORMAL
          state = ParseState::NORMAL;
        }
        else
        {
          --skipDepth;
        }
      }
      // All lines in SKIPPING state are discarded
      continue;
    }

    // NORMAL state
    if (IsCplusplusGuard(dir))
    {
      state = ParseState::SKIPPING;
      skipDepth = 0;
      continue;
    }

    if (dir.name == "include")
    {
      std::string args = dir.args;

      // Check for #include "file" (angle-bracket includes pass through)
      if (!args.empty() && args[0] == '"')
      {
        // Extract filename between quotes
        size_t closeQuote = args.find('"', 1);
        if (closeQuote == std::string::npos || closeQuote == 1)
        {
          std::cerr << "Malformed #include directive in "
                    << canonicalPath.string() << ":" << (i + 1) << std::endl;
          return false;
        }

        std::string includeName = args.substr(1, closeQuote - 1);

        if (includeName.empty())
        {
          std::cerr << "Empty filename in #include directive in "
                    << canonicalPath.string() << ":" << (i + 1) << std::endl;
          return false;
        }

        fs::path includePath;
        try
        {
          includePath = fs::weakly_canonical(parentDir / includeName);
        }
        catch (const std::exception&)
        {
          includePath = parentDir / includeName;
        }

        std::string includeKey = includePath.string();

        if (includedFiles.find(includeKey) == includedFiles.end())
        {
          includedFiles.insert(includeKey);

          std::string includeOut;
          if (!ParseShaderFromFile(includePath, includedFiles, includeOut))
          {
            std::cerr << "  included from " << canonicalPath.string()
                      << ":" << (i + 1) << std::endl;
            return false;
          }
          out += includeOut;
        }
        continue;
      }
      // #include <...> — pass through as-is
    }

    out += line;
    out += '\n';
  }

  return true;
}

static fs::path WriteTempShader(const std::string& source, const fs::path& shaderPath, const std::string& suffix = "")
{
  std::string stem = shaderPath.stem().string();
  std::string ext = shaderPath.extension().string();
  std::string tempName = "shader_tmp_" + stem + (suffix.empty() ? "" : "_" + suffix) + ext + ".glsl";

  fs::path temp = fs::temp_directory_path() / tempName;

  std::ofstream out(temp);
  out << source;
  return temp;
}

static void RemoveTempShader(const fs::path& tempPath)
{
  std::remove(tempPath.string().c_str());
}

static constexpr const char* GLSL_VERSION = "450";

static std::string PrepareSource(const std::string& source, const std::vector<std::string>& defines = {})
{
  std::string body = source;

  // Strip existing #version if present (transition: works with or without it)
  size_t versionPos = body.find("#version");
  if (versionPos != std::string::npos)
  {
    size_t lineEnd = body.find('\n', versionPos);
    if (lineEnd == std::string::npos)
      body.clear();
    else
      body = body.substr(lineEnd + 1);
  }

  std::string result = std::string("#version ") + GLSL_VERSION + "\n";
  for (const auto& def : defines)
    result += "#define " + def + "\n";
  result += body;
  return result;
}

static int RunGlslc(const fs::path& glslc,
                    const fs::path& input,
                    const fs::path& output,
                    const std::string& stage)
{
  std::vector<std::wstring> args = {
    glslc.wstring(),
    L"-g",
    L"-fshader-stage=" + std::wstring(stage.begin(), stage.end()),
    input.wstring(),
    L"-o",
    output.wstring()
  };

  std::wstring cmd;
  for (auto& a : args)
  {
    if (!cmd.empty()) cmd += L" ";
    cmd += L"\"" + a + L"\"";
  }

  // Create a pipe for capturing stderr from glslc
  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  sa.lpSecurityDescriptor = nullptr;

  HANDLE hReadPipe = nullptr;
  HANDLE hWritePipe = nullptr;

  if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
  {
    std::cerr << "Failed to create pipe for glslc stderr" << std::endl;
    return 1;
  }

  // Ensure the read end is not inherited
  SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  PROCESS_INFORMATION pi{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdError = hWritePipe;
  si.hStdOutput = hWritePipe;
  si.hStdInput = nullptr;

  if (!CreateProcessW(
      nullptr,
      cmd.data(),
      nullptr, nullptr,
      TRUE, // inherit handles
      0,
      nullptr, nullptr,
      &si, &pi))
  {
    std::cerr << "Failed to launch glslc" << std::endl;
    CloseHandle(hReadPipe);
    CloseHandle(hWritePipe);
    return 1;
  }

  // Close write end in parent so ReadFile can detect EOF
  CloseHandle(hWritePipe);

  // Read stderr output
  std::string stderrOutput;
  char buffer[4096];
  DWORD bytesRead = 0;

  while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0)
  {
    buffer[bytesRead] = '\0';
    stderrOutput += buffer;
  }

  CloseHandle(hReadPipe);

  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exitCode = 1;
  GetExitCodeProcess(pi.hProcess, &exitCode);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  if (exitCode != 0 && !stderrOutput.empty())
  {
    std::cerr << stderrOutput;
  }

  return static_cast<int>(exitCode);
}

int main(int argc, char** argv)
{
  fs::path sourceDir;
  fs::path outputPath;
  fs::path inputFile;
  fs::path glslc;
  bool singleMode = false;
  std::vector<std::string> defines;

  for (int i = 1; i < argc; ++i)
  {
    std::string arg = argv[i];

    if (arg == "--single")
      singleMode = true;
    else if (arg == "--source" && i + 1 < argc)
      sourceDir = argv[++i];
    else if (arg == "--input" && i + 1 < argc)
      inputFile = argv[++i];
    else if (arg == "--output" && i + 1 < argc)
      outputPath = argv[++i];
    else if (arg == "--glslc" && i + 1 < argc)
      glslc = argv[++i];
    else if (arg.size() > 2 && arg[0] == '-' && arg[1] == 'D')
    {
      std::string def = arg.substr(2);
      size_t eq = def.find('=');
      if (eq != std::string::npos)
        def[eq] = ' ';
      defines.push_back(def);
    }
  }

  if (singleMode)
  {
    if (inputFile.empty() || outputPath.empty() || glslc.empty())
    {
      std::cerr << "Usage: CompileShaders --single --input <file> --output <file> --glslc <path> [-DKEY[=VALUE] ...]\n";
      return 1;
    }

    std::set<std::string> includedFiles;
    std::string text;

    if (!ParseShaderFromFile(inputFile, includedFiles, text))
    {
      std::cerr << "Failed to preprocess shader: " << inputFile.string() << std::endl;
      return 1;
    }

    text = PrepareSource(text, defines);

    auto tempPath = WriteTempShader(text, inputFile, outputPath.stem().string());

    fs::create_directories(outputPath.parent_path());

    std::string stage = inputFile.extension().string().substr(1);

    std::wcout << L"Compiling " << inputFile.filename().wstring();
    if (!defines.empty())
    {
      std::wcout << L" [";
      for (size_t i = 0; i < defines.size(); ++i)
      {
        if (i > 0) std::wcout << L", ";
        std::wcout << std::wstring(defines[i].begin(), defines[i].end());
      }
      std::wcout << L"]";
    }
    std::wcout << std::endl;

    int exitCode = RunGlslc(glslc, tempPath, outputPath, stage);
    RemoveTempShader(tempPath);

    if (exitCode != 0)
    {
      std::cerr << "glslc failed for " << inputFile.string() << " (exit code " << exitCode << ")" << std::endl;
      return 1;
    }

    return 0;
  }

  // Directory mode (original behavior)
  if (sourceDir.empty() || outputPath.empty() || glslc.empty())
  {
    std::cerr << "Usage: CompileShaders --source <dir> --output <dir> --glslc <path>\n";
    return 1;
  }

  auto shaders = CollectShaders(sourceDir);

  bool anyFailed = false;

  for (auto& shader : shaders)
  {
    std::set<std::string> includedFiles;
    std::string text;

    if (!ParseShaderFromFile(shader, includedFiles, text))
    {
      std::cerr << "Failed to preprocess shader: " << shader.string() << std::endl;
      anyFailed = true;
      continue;
    }

    text = PrepareSource(text);

    auto tempPath = WriteTempShader(text, shader);

    fs::path output = outputPath / (shader.filename().string() + ".spv");
    fs::create_directories(output.parent_path());

    std::string stage = shader.extension().string().substr(1);

    std::wcout << L"Compiling " << shader.filename().wstring() << std::endl;

    int exitCode = RunGlslc(glslc, tempPath, output, stage);
    RemoveTempShader(tempPath);

    if (exitCode != 0)
    {
      std::cerr << "glslc failed for " << shader.string() << " (exit code " << exitCode << ")" << std::endl;
      anyFailed = true;
    }
  }

  return anyFailed ? 1 : 0;
}
