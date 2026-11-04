#include <Core/Capybara.h>
#include <JIT/JIT.h>
#include "Core/ReflectionTypes.h"

int main(void) {

  Capybara::Capybara::Init();
  Capybara::Jit::Init("test/dll/");

  Capybara::Jit::SetBuildDirectory(std::filesystem::current_path() / "CapyBuild");

  Capybara::Jit::SetExternalLibrary("build/test/script_core/libScriptCore.a", "test/script_core/src");

  Capybara::Capybara::LoadLibrary("CapyBuild/CapyRuntimeLib.so");

  #if 0
  auto testClass = Capybara::Capybara::GetClass("Test");
  if (testClass) {
    auto instance = testClass->CreateInstance();
    int *mMyIntPtr = instance->GetField<int>("m_MyInt", testClass.get());
    if (mMyIntPtr) {
      *mMyIntPtr = 42;
      std::cout << "Set mMyIntPtr to 42 printing now!\n";
      instance->Invoke<void>("Print", testClass.get());
    }
  }

  #endif

  #if 0

  auto test2Class = Capybara::Capybara::GetClass("Test2");
  if (test2Class) {
    auto instance = test2Class->CreateInstance();
    int myExternalInt = 99;
    instance->Invoke<void>("Print", test2Class.get(), &myExternalInt);
  }

  #endif

  while (1) {
    if (Capybara::Jit::Poll()) {
      Capybara::Capybara::ReloadLibrary("CapyBuild/CapyRuntimeLib.so");
      std::cout << "Library Reloaded!\n";
    }
  }

  Capybara::Capybara::Shutdown();

  return 0;
}
