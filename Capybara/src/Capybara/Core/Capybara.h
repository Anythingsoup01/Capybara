#pragma once

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

  static std::shared_ptr<CapyObject> GetClass(const std::string &className);

  static std::unordered_map<std::string, std::shared_ptr<CapyObject>> &GetClasses() { return m_LoadedClasses; }

private:
  static void PostProcessInheritance(
      std::shared_ptr<CapyObject> targetObj, const ClassLayoutInfo &rawLayout,
      const std::unordered_map<std::string, std::shared_ptr<CapyObject>> &allLoadedClasses);

private:
  static inline std::unordered_map<std::string, std::shared_ptr<CapyObject>>
      m_LoadedClasses;
  static inline void *m_LibraryHandle;
};

} // namespace Capybara
