
#include <capybara/capybara.h>
#include <capybara/runtime.h>

#include <iostream>

#include <dlfcn.h>
#include <ffi.h>

#include <signal.h>

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

    template <typename T>
    void SetFieldData(const std::string& fieldName, T data)
    {
        capy_field_data_set(m_ClassObject, m_CapyClass, fieldName, &data);
    }

    template<typename T>
    T GetFieldData(const std::string& fieldName)
    {
        if (GetFieldDataInternal(fieldName, &s_FieldBufferData))
            return *(T*)s_FieldBufferData;

        return T();
    }


private:
    bool GetFieldDataInternal(const std::string& name, void* buffer)
    {
        capy_field_data_get(m_ClassObject, m_CapyClass, name, buffer);
        return true;
    }

private:
    CapyObject* m_ClassObject;
    CapyClass* m_CapyClass;

    static inline char s_FieldBufferData[8];
};

#define ADD_INTERNAL_CALL(Name) capy_add_internal_call(#Name, (void*)&Name)

int Internal_Add(int a, int b)
{
    return a + b;
}

void CustomFileEventCallback(FileEventType type, const std::filesystem::path& path)
{

    switch (type)
    {
        case FileEventType::Create:
            //std::cout << "CREATED FILE: " << path.filename().string() << "\n";
            break;
        case FileEventType::Modify:
            //std::cout << "MODIFIED FILE: " << path.filename().string() << "\n";
            break;
        case FileEventType::Delete:
            //std::cout << "DELETED FILE: " << path.filename().string() << "\n";
            break;
    }
}


int main(int argc, char** argv) 
{
    auto* cd = capy_init();

    capy_set_source_path("test/dll/Test", CustomFileEventCallback, true);

    capy_set_core_bin_include_path("test/dll/Base");

    capy_domain_library_open("build/test/dll/Base/libbase-class.so", true);

    capy_reload_libraries_into_domain();

    CapyLibrary* lib = capy_domain_library_open("test-lib.so", false);

#if 1
    CapyImage* img = capy_library_get_image(lib);

    ADD_INTERNAL_CALL(Internal_Add);

    CapyWrapper TestClassWrapper(img, "DLL", "Test");

    int data = 10;
    TestClassWrapper.SetFieldData(".ID", data);

    CapyMethod* PrintBaseIntMethod = TestClassWrapper.GetMethod("PrintBaseInt");

    if (!TestClassWrapper.InvokeMethod(PrintBaseIntMethod))
        raise(SIGTRAP);


    int retrievedData = TestClassWrapper.GetFieldData<int>(".ID");

    std::cout << "Retrieved Data: " << retrievedData << "\n";


    while (true) 
    {
        if (capy_poll())
        {
            std::cout << capy_dump_domain();
        }
    }
#endif

    capy_shutdown();

    return 0;
}
