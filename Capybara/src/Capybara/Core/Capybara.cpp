#include "Capybara.h"
#include "LibraryLoader.h"
#include "ObjectAllocator.h"

#include "ReflectionTypes.h"

#include <dlfcn.h>
#include <gelf.h>

namespace Capybara {

void Capybara::Init() {
  if (elf_version(EV_CURRENT) == EV_NONE) {
    std::runtime_error("ELF library failed to initialize! 'EV_NONE'");
  }
}

void Capybara::Shutdown() {
}

void Capybara::LoadLibrary(const std::filesystem::path &libPath) {
  UnloadLibrary();

  m_LibraryHandle = dlopen(libPath.c_str(), RTLD_NOW | RTLD_DEEPBIND);
  if (!m_LibraryHandle) {
    UnloadLibrary();
    throw std::runtime_error(dlerror());
  }

  LibraryLoader loader(libPath);

  const auto &rawClasses = loader.GetClasses();
  for (const auto &[className, layout] : rawClasses) {
    auto capyObj = ObjectAllocator::CreateCapyObject(m_LibraryHandle, layout);
    m_LoadedClasses[className] = capyObj;
  }

  for (const auto &[className, classLayout] : rawClasses) {
    auto capyObj = m_LoadedClasses[className];
    if (capyObj) {
      PostProcessInheritance(capyObj, classLayout, m_LoadedClasses);
    }
  }
}

void Capybara::ReloadLibrary(const std::filesystem::path &libPath) {
  LoadLibrary(libPath);
}

void Capybara::UnloadLibrary() {
  if (m_LibraryHandle) {
    m_LoadedClasses.clear();
    dlclose(m_LibraryHandle);
    m_LibraryHandle = nullptr;
  }
}

std::shared_ptr<CapyObject> Capybara::GetClass(const std::string& className) {
  auto it = m_LoadedClasses.find(className);
  if (it != m_LoadedClasses.end()) {
    return it->second;
  }
  return nullptr;
}

void Capybara::PostProcessInheritance(
  std::shared_ptr<CapyObject> targetObj,
  const ClassLayoutInfo& rawLayout,
  const std::unordered_map<std::string, std::shared_ptr<CapyObject>>& allLoadedClasses) {
  // 1. Assign base classes from raw layout data
  targetObj->BaseClasses = rawLayout.BaseClasses;

  // 2. Inherit Methods from Base Classes
  for (const auto& baseInfo : targetObj->BaseClasses) {
    auto baseIt = allLoadedClasses.find(baseInfo.ClassName);
    if (baseIt != allLoadedClasses.end() && baseIt->second) {
      const auto& baseMethods = baseIt->second->Methods;

      for (const auto& baseMethod : baseMethods) {
        // Check if the derived class overrides this method
        bool overridden = false;
        for (const auto& derivedMethod : targetObj->Methods) {
          if (derivedMethod.Name == baseMethod.Name) {
            overridden = true;
            break;
          }
        }

        // If not overridden, inherit the base method symbol
        if (!overridden) {
          targetObj->Methods.push_back(baseMethod);
        }
      }
    }
  }
}

}
