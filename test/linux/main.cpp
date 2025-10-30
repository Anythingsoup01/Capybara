#include "Capybara.h"
#include "Runtime.hpp"
#include <iostream>

#include <dlfcn.h>
#include <ffi.h>


class CapyWrapper
{
public:
    CapyWrapper(const std::string& classNamespace, const std::string& className)
		: m_ClassNamespace(classNamespace), m_ClassName(className)
	{
        m_CapyClass = Capybara::GetClassObject(classNamespace, className);
	}
	CapyObject* Instantiate()
	{
		return nullptr;
	}
	MethodEntry* GetMethod(const char* methodName, int parameterCount)
	{
		//return mono_class_get_method_from_name(m_MonoClass, methodName, parameterCount);
	}
	CapyObject* InvokeMethod(CapyObject* instance, MethodEntry* method, void** params)
	{
		//return mono_runtime_invoke(method, instance, params, nullptr);
	}
private:
    std::string m_ClassNamespace, m_ClassName;
    CapyObject* m_CapyClass;
    
};


int main(int argc, char** argv) 
{
    Capybara::InitCapy();

    auto obj = Capybara::CreateObject();
    Capybara::CallMethod(obj.get(), "ToString");


    auto s = Capybara::CreateManagedString("Hello from Managed String!");
    Capybara::CallMethod(s.get(), "ToString");


    void* valPtr = Capybara::CallMethod(static_cast<CapyObject*>(s.get()), "GetValue");
    if (valPtr)
    {
        auto strPtr = reinterpret_cast<std::string*>(valPtr);
        std::cout << "From the GetValue(): " << *strPtr << std::endl;
    }

    bool status = Capybara::AddLibrary("test/dll/test-lib.so");

    if (!status)
    {
        std::cerr << "Failed to load library!\n";
        return -1;
    }

    auto testLib = Capybara::GetObject("test-lib.so");
    
    CapyWrapper testClass("DLL", "Test");

    void* classOBJ = Capybara::CallMethod(testLib, "Create");



    Capybara::CallMethod(testLib, "Print", { classOBJ, "This is a test"  });

    Capybara::CallMethod(testLib, "PrintAgain", { classOBJ });

    Capybara::CallMethod(testLib, "Add", { classOBJ, 10, 12 });

    return 0;
}
