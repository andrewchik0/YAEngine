#include <iostream>
#include <filesystem>
#include <fstream>
#include <set>
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

bool ReadShaderFile(const std::string& path, std::string& out)
{
  std::ifstream file(path);

  if (!file.is_open())
    return false;

  out = std::string(std::istreambuf_iterator(file), std::istreambuf_iterator<char>());

  return true;
}

bool ParseShaderFromFile(const std::string& path, std::set<std::string>& includedFiles, std::string& out)
{
  std::filesystem::path fpath(path);
  size_t pos = 0;
  std::string text;
  auto succes = ReadShaderFile(path, text);

  if (!succes)
  {
    std::cerr << "Failed to read shader file: " << path << std::endl;
    return false;
  }

  for (auto it = text.begin(); it < text.end(); ++it)
  {
    if (text.size() > pos + 18 && *it == '#' && std::string(it + 1, it + 18) == "ifdef __cplusplus")
    {
      while (*it != '#' || std::string(it + 1, it + 6) != "endif")
        ++it, pos++;
      it += 6, pos += 6;
    }

    if (*it == '#' && std::string(it + 1, it + 8) == "include")
    {
      it += 8, pos += 8;
      while(*it++ != '\"') pos++;
      auto start = it;
      while(*it++ != '\"') pos++;
      auto end = it - 1;

      auto filename =
        fpath.parent_path().string() +
        '/' +
        std::string(start, end);

      if (includedFiles.find(filename) == includedFiles.end())
      {
        includedFiles.insert(filename);
        std::string parseOut;
        if (auto parseStatus = ParseShaderFromFile(filename, includedFiles, parseOut); parseStatus != true)
        {
          std::cerr << "Failed to parse shader file: " << path << std::endl;
          return parseStatus;
        }
        out += parseOut;
      }
    }
    out += *it;
  }
  return true;
}

fs::path WriteTempShader(const std::string& source)
{
  fs::path temp =
      fs::temp_directory_path() / "shader_tmp.glsl";

  std::ofstream out(temp);
  out << source;
  return temp;
}

void RemoveTempShader()
{
  std::remove((fs::temp_directory_path() / "shader_tmp.glsl").string().c_str());
}

void RunGlslc(const fs::path& glslc,
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

  STARTUPINFOW si{};
  PROCESS_INFORMATION pi{};
  si.cb = sizeof(si);

  if (!CreateProcessW(
      nullptr,
      cmd.data(),
      nullptr, nullptr,
      FALSE,
      0,
      nullptr, nullptr,
      &si, &pi))
  {
    throw std::runtime_error("Failed to launch glslc");
  }

  WaitForSingleObject(pi.hProcess, INFINITE);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
}

int main(int argc, char** argv)
{
  fs::path sourceDir;
  fs::path outputDir;
  fs::path glslc;

  for (int i = 1; i < argc; ++i)
  {
    std::string arg = argv[i];

    if (arg == "--source" && i + 1 < argc)
      sourceDir = argv[++i];
    else if (arg == "--output" && i + 1 < argc)
      outputDir = argv[++i];
    else if (arg == "--glslc" && i + 1 < argc)
      glslc = argv[++i];
  }

  if (sourceDir.empty() || outputDir.empty() || glslc.empty())
  {
    std::cerr << "Usage: CompileShaders --source <dir> --output <dir> --glslc <path>\n";
    return 1;
  }

  auto shaders = CollectShaders(sourceDir);

  for (auto& shader : shaders)
  {
    std::set<std::string> includedFiles;
    std::string text;
    ParseShaderFromFile(shader.string(), includedFiles, text);

    auto tempPath = WriteTempShader(text);

    fs::path input = fs::temp_directory_path() / "shader_tmp.glsl";
    fs::path output = outputDir / (shader.filename().string() + ".spv");

    fs::create_directories(output.parent_path());

    std::wcout << L"Compiling " << shader.filename() << std::endl;
    RunGlslc(glslc, input, output, &shader.extension().string()[1]);
    RemoveTempShader();
  }

  return 0;
}