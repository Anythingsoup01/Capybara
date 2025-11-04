#include "Capybara.h"
#include "Runtime.hpp"
#include <iostream>

#include <dlfcn.h>
#include <ffi.h>


class CapyWrapper
{
public:
    CapyWrapper(CapyImage* ci, const std::string& nameSpace, const std::string& className)
		: m_ClassNamespace(nameSpace), m_ClassName(className)
	{
        m_CapyClass = capy_class_from_name(ci, nameSpace, className);
	}
	CapyMethod* GetMethod(const char* methodName)
	{
		return capy_method_from_class(m_CapyClass, methodName);
	}
	void* InvokeMethod(CapyMethod* method, const std::vector<RuntimeValue>& params)
	{
		return capy_function_call_from_method(method, params);
	}

private:
    std::string m_ClassNamespace, m_ClassName;
    CapyClass* m_CapyClass;
};


int main(int argc, char** argv) 
{
    capy_set_libraries_path("build");

    CapyDomain* d = capy_init_domain("Expansion");

    capy_reload_libraries_into_domain(d);

    CapyLibrary* l = capy_domain_library_open(d, "libtest-lib.so");

    CapyImage* i = capy_library_get_image(l);

    CapyWrapper cw(i, "DLL", "Test");


    CapyMethod* m = cw.GetMethod("Create");

    void* valPtr = cw.InvokeMethod(m, {});
    if (valPtr)
    {
        std::cout << "Got: " << valPtr << std::endl;
    }

    capy_unload_domain("Expansion");

    return 0;
}
