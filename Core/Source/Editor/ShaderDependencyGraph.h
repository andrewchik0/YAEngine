#pragma once

#include "Pch.h"

namespace YAEngine
{
  struct ShaderPermutation
  {
    std::string sourceFile;
    std::string outputName;
    std::vector<std::string> defines;
  };

  struct CompileTask
  {
    std::string sourceFile;
    std::string outputName;
    std::vector<std::string> defines;
  };

  class ShaderDependencyGraph
  {
  public:
    void Build(const std::string& shaderDir, const std::string& sharedDir);
    void RegisterPermutation(const std::string& sourceFile, const std::string& outputName,
                             const std::vector<std::string>& defines);
    void LoadPermutationsFromCSV(const std::string& csvPath);

    std::vector<CompileTask> PollChanges();
    std::vector<CompileTask> GetAllCompileTasks() const;

  private:
    void ScanFile(const std::filesystem::path& filePath, const std::filesystem::path& baseDir);
    void CollectRootShaders(const std::string& changedFile, std::unordered_set<std::string>& roots, std::unordered_set<std::string>& visited) const;
    bool IsCompilable(const std::string& filename) const;

    // reverse graph: included file → files that include it
    std::unordered_map<std::string, std::vector<std::string>> m_IncludedBy;

    // last known modification time
    std::unordered_map<std::string, std::filesystem::file_time_type> m_LastWriteTime;

    // all tracked file paths (canonical)
    std::unordered_map<std::string, std::filesystem::path> m_FilenameToPaths;

    // permutations: source filename → list of permutations
    std::unordered_map<std::string, std::vector<ShaderPermutation>> m_Permutations;
  };
}
