#pragma once

#include "FileWatcher.h"
#include "JITCompiler.h"

namespace Capybara {

class Jit {
public:
  Jit() = default;
  ~Jit() = default;

  Jit(const Jit &) = delete;
  Jit &operator=(Jit &) = delete;

  Jit(Jit &&) = delete;
  Jit &operator=(Jit &&) = delete;
public:
  static void Init(const std::filesystem::path &sourceDirectory);
  static void Shutdown();

  static void SetExternalLibrary(const std::filesystem::path &binaryPath, const std::filesystem::path &includePath) {
    m_Compiler.SetExternalLibrary(binaryPath, includePath);
  }

  static void SetBuildDirectory(const std::filesystem::path &buildDir) {
    m_Compiler.SetBuildDirectory(buildDir);
  }

  static bool ForceCompilation() {
    return m_Compiler.ForceCompilation();
  }

  static bool Poll();
private:
  static inline FileWatcher m_FileWatcher;
  static inline JitCompiler m_Compiler;
};

}
