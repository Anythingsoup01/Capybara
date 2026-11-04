#include <Capybara/Core/Capybara.h>
#include <Capybara/JIT/JIT.h>
#include <Capybara/Core/ReflectionTypes.h>
#include <dlfcn.h>

#define ADD_INTERNAL_CALL(Name) Capybara::Capybara::AddInternalCall(#Name, (void *)&Name)


void external_adder(int a, int b) {
  std::cout << a + b << "\n";
}

int main(void) {
  Capybara::Capybara::Init();
  Capybara::Jit::Init("test/dll/");

  Capybara::Jit::SetBuildDirectory(std::filesystem::current_path() / "CapyBuild");

  Capybara::Capybara::SetExternalLibrary("build/test/script_core/libScriptCore.so", "test/script_core/src");

  ADD_INTERNAL_CALL(external_adder);

  //Capybara::Capybara::LoadLibrary("CapyBuild/CapyRuntimeLib.so");

  #if 1
  auto testClass = Capybara::Capybara::GetClass("Test");
  if (testClass) {
    auto instance = testClass->CreateInstance();
    int *mMyIntPtr = new int();
    instance->GetField("m_MyInt", testClass.get(), mMyIntPtr);
    if (mMyIntPtr) {
      *mMyIntPtr = 42;
      std::cout << "Set mMyIntPtr to 42 printing now!\n";
      instance->Invoke<void>("Print", testClass.get());
    }
    free(mMyIntPtr);
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
