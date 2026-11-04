#pragma once

#include <JIT/FileWatcher.h>

namespace Capybara {

class JitCompiler {
public:
  JitCompiler() = default;

  void SetExternalLibrary(const std::filesystem::path &binaryPath, const std::filesystem::path &includePath) {
    m_ExternalBinaryPath = binaryPath;
    m_ExternalIncludePath = includePath;
  }

  void SetBuildDirectory(const std::filesystem::path &buildDir) {
    m_BuildDir = buildDir;
  }

  void SetSourcePath(const std::filesystem::path &sourcePath) {
    m_SourcePath = sourcePath;
  }

  bool ProcessChanges(const std::vector<FileChangeEvent> &changes);

private:
  std::filesystem::path GetObjectPath(const std::filesystem::path &srcPath);

  bool CompileSingleFile(const std::filesystem::path &srcPath, const std::filesystem::path &objPath);

  void CleanupOrphanBinaries();

  void GenerateCompileCommands();

  bool LinkAll();

private:
  std::filesystem::path m_ExternalBinaryPath = "";
  std::filesystem::path m_ExternalIncludePath = "";
  std::filesystem::path m_BuildDir = "";
  std::filesystem::path m_SourcePath = "";
};

} // namespace Capybara
