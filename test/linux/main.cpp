
#include <capybara/capybara.h>
#include <capybara/runtime.h>

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

#define ADD_INTERNAL_CALL(Name) capy_add_internal_call(#Name, (void*)&Name)

int Internal_Add(int a, int b)
{
    return a + b;
}

std::filesystem::path CustomFileEventCallback(FileEventType type, const std::filesystem::path& path)
{

    switch (type)
    {
        case FileEventType::Create:
            std::cout << "CREATED FILE: " << path.filename().string() << "\n";
            break;
        case FileEventType::Modify:
            std::cout << "MODIFIED FILE: " << path.filename().string() << "\n";
            break;
        case FileEventType::Delete:
            std::cout << "DELETED FILE: " << path.filename().string() << "\n";
            break;
    }

    return path;
}


int main(int argc, char** argv) 
{
    auto* cd = capy_jit_init();

    capy_jit_set_ignore_hidden_paths(true);
    capy_jit_set_source_path("test/dll/Test", true);

    capy_jit_set_binary_path("test/dll/Test/.build");

    capy_jit_set_core_bin_include_path("test/dll/Base");


    capy_jit_set_fs_event_callback(CustomFileEventCallback);

    capy_set_ignored_classname({"IgnoreThis"});


    capy_domain_core_library_open("build/test/dll/Base/libbase-class.so");

    capy_reload_libraries_into_domain();

    while (true) 
    {
        capy_jit_update_fs_event_watcher();

        if (capy_jit_poll())
        {
            std::cout << capy_dump_domain();
        }
    }

#   if 0
    CapyDomain* d;

    CapyLibrary* l = capy_domain_library_open(d, "libtest-lib.so", false);

    CapyImage* i = capy_library_get_image(l);

    CapyWrapper cw(i, "DLL", "Test");

    ADD_INTERNAL_CALL(Internal_Add);

    CapyMethod* cm = cw.GetMethod("Create");
    CapyMethod* pm = cw.GetMethod("Print");
    CapyMethod* pam = cw.GetMethod("PrintAgain");
    CapyMethod* am = cw.GetMethod("CustomAdd");

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
#   endif
    capy_jit_shutdown();

    return 0;
}
