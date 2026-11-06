#pragma once

#include "Capybara/JIT/JIT.h"

namespace Capybara {

struct CapyObject;
struct ClassLayoutInfo;

class Capybara {
public:
  Capybara() = default;
  ~Capybara() = default;

  Capybara(const Capybara &) = delete;
  Capybara &operator=(Capybara &) = delete;

  Capybara(Capybara &&) = delete;
  Capybara &operator=(Capybara &&) = delete;

public:
  static void Init();
  static void Shutdown();

  static void LoadLibrary(const std::filesystem::path &libPath);
  static void ReloadLibrary(const std::filesystem::path &libPath);
  static void UnloadLibrary();

  static void SetExternalLibrary(const std::filesystem::path &binPath, const std::filesystem::path &includePath) {
    m_ExternalBinaryPath = binPath;
    Jit::SetExternalLibrary(binPath, includePath);
  }
  
  static std::shared_ptr<CapyObject> GetClass(const std::string &className);
  static std::unordered_map<std::string, std::shared_ptr<CapyObject>> &GetClasses() { return m_LoadedClasses; }

  static void AddInternalCall(const std::string &name, void *funcPointer);

private:
  static void PostProcessInheritance(
      std::shared_ptr<CapyObject> targetObj, const ClassLayoutInfo &rawLayout,
      const std::unordered_map<std::string, std::shared_ptr<CapyObject>> &allLoadedClasses);

private:
  static inline std::filesystem::path m_ExternalBinaryPath;
  static inline std::filesystem::path m_TempExternalLibPath;
  static inline std::filesystem::path m_TempLibraryPath;
  static inline void *m_ExternalLibraryHandle;
  static inline std::unordered_map<std::string, std::shared_ptr<CapyObject>> m_LoadedClasses;
  static inline void *m_LibraryHandle;
  static inline std::unordered_map<std::string, void *> m_InternalCallMap;
};

} // namespace Capybara
