#include "Capybara.h"
#include "Runtime.hpp"
#include <iostream>

#include <dlfcn.h>
#include <ffi.h>

/*
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
*/

int main(int argc, char** argv) 
{
    capy_init();

    CapyDomain* d = capy_init_domain("Expansion");

    CapyLibrary* l = capy_domain_library_open(d, "./test/dll/test-lib.so");

    CapyImage* i = capy_library_get_image(l);

    CapyClass* c = capy_class_from_name(i, "DLL", "Test");

    CapyMethod* m = capy_method_from_class(c, "Create");

    void* valPtr = capy_function_call_from_method(m, {});
    if (valPtr)
    {
        std::cout << "Got: " << valPtr << std::endl;
    }

    return 0;
}
