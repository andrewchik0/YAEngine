#include "Editor/ShaderDependencyGraph.h"
#include "Utils/Log.h"

namespace YAEngine
{
  static std::string NormalizePath(const std::filesystem::path& p)
  {
    return std::filesystem::weakly_canonical(p).string();
  }

  bool ShaderDependencyGraph::IsCompilable(const std::string& filename) const
  {
    auto ext = std::filesystem::path(filename).extension().string();
    return ext == ".vert" || ext == ".frag" || ext == ".comp" || ext == ".geom";
  }

  void ShaderDependencyGraph::ScanFile(const std::filesystem::path& filePath, const std::filesystem::path& baseDir)
  {
    std::string canonical = NormalizePath(filePath);
    std::string filename = filePath.filename().string();

    m_FilenameToPaths[canonical] = filePath;

    std::error_code ec;
    auto writeTime = std::filesystem::last_write_time(filePath, ec);
    if (!ec)
      m_LastWriteTime[canonical] = writeTime;

    std::ifstream file(filePath);
    if (!file.is_open())
      return;

    std::string line;
    while (std::getline(file, line))
    {
      // Parse #include "..." directives
      auto pos = line.find("#include");
      if (pos == std::string::npos)
        continue;

      auto quote1 = line.find('"', pos + 8);
      if (quote1 == std::string::npos)
        continue;

      auto quote2 = line.find('"', quote1 + 1);
      if (quote2 == std::string::npos)
        continue;

      std::string includePath = line.substr(quote1 + 1, quote2 - quote1 - 1);
      auto resolvedPath = filePath.parent_path() / includePath;

      if (!std::filesystem::exists(resolvedPath))
        continue;

      std::string includedCanonical = NormalizePath(resolvedPath);

      // Reverse edge: the included file is included BY this file
      m_IncludedBy[includedCanonical].push_back(canonical);
    }
  }

  void ShaderDependencyGraph::Build(const std::string& shaderDir, const std::string& sharedDir)
  {
    m_IncludedBy.clear();
    m_LastWriteTime.clear();
    m_FilenameToPaths.clear();

    auto scanDir = [this](const std::string& dir)
    {
      if (!std::filesystem::exists(dir))
        return;

      for (auto& entry : std::filesystem::recursive_directory_iterator(dir))
      {
        if (!entry.is_regular_file())
          continue;

        auto ext = entry.path().extension().string();
        if (ext == ".vert" || ext == ".frag" || ext == ".comp" || ext == ".geom"
            || ext == ".glsl" || ext == ".h")
        {
          ScanFile(entry.path(), dir);
        }
      }
    };

    scanDir(shaderDir);
    scanDir(sharedDir);

    uint32_t fileCount = static_cast<uint32_t>(m_FilenameToPaths.size());
    uint32_t edgeCount = 0;
    for (auto& [_, deps] : m_IncludedBy)
      edgeCount += static_cast<uint32_t>(deps.size());

    YA_LOG_INFO("ShaderHotReload", "Dependency graph: %u files, %u edges", fileCount, edgeCount);
  }

  void ShaderDependencyGraph::RegisterPermutation(const std::string& sourceFile, const std::string& outputName,
                                                   const std::vector<std::string>& defines)
  {
    m_Permutations[sourceFile].push_back({ sourceFile, outputName, defines });
  }

  void ShaderDependencyGraph::LoadPermutationsFromCSV(const std::string& csvPath)
  {
    std::ifstream file(csvPath);
    if (!file.is_open())
    {
      YA_LOG_WARN("ShaderHotReload", "Could not open permutations file: %s", csvPath.c_str());
      return;
    }

    uint32_t count = 0;
    std::string line;
    while (std::getline(file, line))
    {
      if (line.empty())
        continue;

      // Format: source,output,DEFINE1 DEFINE2 ...
      auto comma1 = line.find(',');
      if (comma1 == std::string::npos)
        continue;

      auto comma2 = line.find(',', comma1 + 1);
      if (comma2 == std::string::npos)
        continue;

      std::string source = line.substr(0, comma1);
      std::string output = line.substr(comma1 + 1, comma2 - comma1 - 1);
      std::string definesStr = line.substr(comma2 + 1);

      std::vector<std::string> defines;
      if (!definesStr.empty())
      {
        std::istringstream iss(definesStr);
        std::string def;
        while (iss >> def)
          defines.push_back(std::move(def));
      }

      RegisterPermutation(source, output, defines);
      ++count;
    }

    YA_LOG_INFO("ShaderHotReload", "Loaded %u permutation(s) from CSV", count);
  }

  void ShaderDependencyGraph::CollectRootShaders(const std::string& changedFile,
                                                  std::unordered_set<std::string>& roots) const
  {
    // If this file is itself a compilable shader, it's a root
    std::string filename = std::filesystem::path(changedFile).filename().string();
    if (IsCompilable(filename))
      roots.insert(changedFile);

    // Walk reverse edges to find all files that (transitively) include this one
    auto it = m_IncludedBy.find(changedFile);
    if (it == m_IncludedBy.end())
      return;

    for (auto& parent : it->second)
    {
      if (!roots.contains(parent))
        CollectRootShaders(parent, roots);
    }
  }

  std::vector<CompileTask> ShaderDependencyGraph::PollChanges()
  {
    std::vector<std::string> changedFiles;

    for (auto& [canonical, lastTime] : m_LastWriteTime)
    {
      std::error_code ec;
      auto currentTime = std::filesystem::last_write_time(m_FilenameToPaths[canonical], ec);
      if (ec)
        continue;

      if (currentTime != lastTime)
      {
        lastTime = currentTime;
        changedFiles.push_back(canonical);
      }
    }

    if (changedFiles.empty())
      return {};

    // Collect all root shaders affected by any changed file
    std::unordered_set<std::string> affectedRoots;
    for (auto& changed : changedFiles)
      CollectRootShaders(changed, affectedRoots);

    // Generate compile tasks
    std::vector<CompileTask> tasks;
    std::unordered_set<std::string> addedOutputs;

    for (auto& rootCanonical : affectedRoots)
    {
      std::string filename = std::filesystem::path(rootCanonical).filename().string();

      // Check if this file has registered permutations
      auto permIt = m_Permutations.find(filename);
      if (permIt != m_Permutations.end())
      {
        for (auto& perm : permIt->second)
        {
          if (addedOutputs.insert(perm.outputName).second)
            tasks.push_back({ perm.sourceFile, perm.outputName, perm.defines });
        }
      }

      // Always add the default compilation (source.ext → source.ext.spv)
      std::string defaultOutput = filename + ".spv";
      if (addedOutputs.insert(defaultOutput).second)
        tasks.push_back({ filename, defaultOutput, {} });
    }

    return tasks;
  }

  std::vector<CompileTask> ShaderDependencyGraph::GetAllCompileTasks() const
  {
    std::vector<CompileTask> tasks;
    tasks.reserve(m_FilenameToPaths.size());
    std::unordered_set<std::string> addedOutputs;

    for (auto& [canonical, _] : m_FilenameToPaths)
    {
      std::string filename = std::filesystem::path(canonical).filename().string();
      if (!IsCompilable(filename))
        continue;

      auto permIt = m_Permutations.find(filename);
      if (permIt != m_Permutations.end())
      {
        for (auto& perm : permIt->second)
        {
          if (addedOutputs.insert(perm.outputName).second)
            tasks.push_back({ perm.sourceFile, perm.outputName, perm.defines });
        }
      }

      std::string defaultOutput = filename + ".spv";
      if (addedOutputs.insert(defaultOutput).second)
        tasks.push_back({ filename, defaultOutput, {} });
    }

    return tasks;
  }
}
