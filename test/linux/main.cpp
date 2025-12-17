
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
        m_ClassObject = capy_instantiate_object(m_CapyClass);
	}
	CapyMethod* GetMethod(const char* methodName)
	{
        if (m_CapyClass)
		    return capy_method_from_class(m_CapyClass, methodName);
	    return nullptr;
    }
	void* InvokeMethod(CapyMethod* method, const std::vector<RuntimeValue>& params = {})
	{
        if (!method)
            return nullptr;

        std::vector<RuntimeValue> localCopy = params;

        localCopy.insert(localCopy.begin(), m_ClassObject->Memory);

		return capy_function_call_from_method(method, localCopy);
	}

private:
    CapyObject* m_ClassObject;
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

    capy_jit_set_source_path("test/dll/Test", CustomFileEventCallback, true);

    capy_jit_set_binary_path("test/dll/Test/.build");

    capy_jit_set_core_bin_include_path("test/dll/Base");

    capy_domain_core_library_open("build/test/dll/Base/libbase-class.so");

    capy_reload_libraries_into_domain();

    CapyLibrary* lib = capy_domain_library_open("test-lib.so");

    CapyImage* img = capy_library_get_image(lib);

    ADD_INTERNAL_CALL(Internal_Add);

    CapyWrapper TestClassWrapper(img, "DLL", "Test");

    CapyMethod* PrintMethod = TestClassWrapper.GetMethod("Print");
    CapyMethod* PrintAgainMethod = TestClassWrapper.GetMethod("PrintAgain");
    CapyMethod* AddMethod = TestClassWrapper.GetMethod("CustomAdd");

    TestClassWrapper.InvokeMethod(PrintMethod, {"This is my message!"});

    TestClassWrapper.InvokeMethod(PrintAgainMethod);

    TestClassWrapper.InvokeMethod(AddMethod, {10, 15});

    while (true) 
    {
        if (capy_jit_poll())
        {
            std::cout << capy_dump_domain();
        }
    }

    capy_jit_shutdown();

    return 0;
}
