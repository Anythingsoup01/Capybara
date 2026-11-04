#include "JIT.h"

namespace Capybara {

void Jit::Init(const std::filesystem::path &sourceDirectory) {
  m_FileWatcher.Init(sourceDirectory);
  m_Compiler.SetSourcePath(sourceDirectory);
}

void Jit::Shutdown() {

}

bool Jit::Poll() {
  return m_Compiler.ProcessChanges(m_FileWatcher.Poll());
}

}
