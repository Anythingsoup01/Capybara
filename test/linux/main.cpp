#include "Capybara.h"
#include "Runtime.hpp"
#include <iostream>

#include <dlfcn.h>
#include <ffi.h>


class CapyWrapper
{
public:
    CapyWrapper(CapyImage* ci, const char* nameSpace, const char* className)
	{
        m_CapyClass = capy_class_from_name(ci, nameSpace, className);
	}
	CapyMethod* GetMethod(const char* methodName)
	{
        if (m_CapyClass)
		    return capy_method_from_class(m_CapyClass, methodName);
	    return nullptr;
    }
	void* InvokeMethod(CapyMethod* method, const std::vector<RuntimeValue>& params)
	{
        if (!method)
            return nullptr;

		return capy_function_call_from_method(method, params);
	}

private:
    CapyClass* m_CapyClass;
};

static int Internal_Add(int a, int b)
{
    return a + b;
}


int main(int argc, char** argv) 
{
    capy_init();

    capy_set_libraries_path("build");

    capy_set_ignored_classname({"IgnoreThis"});

    CapyDomain* d = capy_init_domain("Expansion");


    capy_domain_library_open(d, "libbase-class.so", true);

    capy_reload_libraries_into_domain(d);

    capy_add_internal_call("Internal_Add", (void*)&Internal_Add);

    CapyLibrary* l = capy_domain_library_open(d, "libtest-lib.so", false);

    CapyImage* i = capy_library_get_image(l);

    CapyWrapper cw(i, "DLL", "Test");


    CapyMethod* cm = cw.GetMethod("Create");
    CapyMethod* pm = cw.GetMethod("Print");
    CapyMethod* pam = cw.GetMethod("PrintAgain");
    CapyMethod* am = cw.GetMethod("Add");

    void* valPtr = cw.InvokeMethod(cm, {});

    cw.InvokeMethod(pm, {cm, "This is my message!"});

    cw.InvokeMethod(pam, {cm});

    cw.InvokeMethod(am, {cm, 10, 15});

    auto coreLibs = capy_get_core_libraries_from_domain("Expansion");

    for (auto& libName : coreLibs)
    {
        std::cout << libName << "\n";
    }

    std::cout << capy_dump_domain("Expansion");

    capy_unload_domain("Expansion");

    return 0;
}
