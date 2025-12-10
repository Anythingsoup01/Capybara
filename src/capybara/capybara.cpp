#include "capybara/runtime.h"
#include "cpypch.h"

#include "capybara/capybara.h"
#include "libelf_util.h"
#include "ffi_util.h"


static Storage s_Storage;


static void update_symbol_namespaces(std::vector<SymbolMetaData>& symbols)
{
    for (auto& sym : symbols)
    {
        std::string& NameSpace = sym.Namespace;
        //std::cout << "NAMESPACE: " << NameSpace << "::" << sym.Name << "\n";
        for (auto& knownName : s_Storage.KnownClassNames)
        {
            size_t foundName = NameSpace.rfind(knownName);
            if (foundName != std::string::npos)
            {
                std::string className = NameSpace.substr(foundName);
                if (className.length() != knownName.length()) continue;
                if (foundName - 2 != std::string::npos)
                    NameSpace = NameSpace.substr(0, foundName - strlen("::"));
                else
                    NameSpace.clear();

                sym.ClassName = knownName;
                break;
            }
        }
    }
}


void capy_init()
{
    s_Storage = Storage();
    s_Storage.IgnoredNamespaces = { "std", "__gnu", "<anon>", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "_IO_", "_G_" };
    s_Storage.IgnoredClassNames = { "std", "__gnu", "<anon>", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "_IO_", "_G_", "__pthread", "timespec", "lconv", "_M_" };
    s_Storage.IgnoredNames = { "gp_offset", "fp_offset", "overflow_arg_area", "reg_save_area", "tm_", "_vptr." };
    s_Storage.IgnoreEmptyNamespaces = false;
}

void capy_shutdown()
{
    for (auto& [name, domain] : s_Storage.Domains)
    {
        CapyDomain* d = domain.get();
        capy_unload_domain(name);
    }
}

void capy_set_libraries_path(const std::filesystem::path &libPath)
{
    s_Storage.SearchPath = libPath;
}

void capy_set_ignored_namespace(const std::vector<std::string>& ignoredNamespace)
{
    for (auto& nameSpace : ignoredNamespace)
        s_Storage.IgnoredNamespaces.push_back(nameSpace);
}

void capy_set_ignore_empty_namespace(bool active)
{
    s_Storage.IgnoreEmptyNamespaces = active;
}

void capy_set_ignored_classname(const std::vector<std::string>& ignoredClassName)
{
    for (auto& className : ignoredClassName)
        s_Storage.IgnoredClassNames.push_back(className);
}

CapyDomain* capy_init_domain(const std::string& name)
{
    if (s_Storage.Domains.find(name) != s_Storage.Domains.end())
    {
        std::cerr << "ERROR: domain '" << name << "' already exists!\n";
        return nullptr;
    }

    auto domain = std::make_unique<CapyDomain>();
    CapyDomain* ptr = domain.get();
    s_Storage.Domains[name] = std::move(domain);
    return ptr;
}

void capy_unload_domain(const std::string& domainName)
{
    auto it = s_Storage.Domains.find(domainName);
    if (it == s_Storage.Domains.end())
    {
        std::cout << "Domain: " << domainName << " doesn't exist!\n";
        return;
    }

    s_Storage.Domains.erase(it);
    s_Storage.InternalCalls.clear();
}

std::string capy_dump_domain(const std::string& domainName)
{
    if (s_Storage.Domains.find(domainName) == s_Storage.Domains.end())
    {
        std::cerr << "capy_dump_domain: Domain '" << domainName << "' doesn't exist!\n";
        return "<null>\n";
    }
    std::string str = "Domain: " + domainName + "\n";
    CapyDomain* domain = s_Storage.Domains[domainName].get();
    for (auto& [libName, lib] : domain->Libraries)
    {
        str.append("  Library: " + libName);
        if (lib->IsCore)
        {
            str.append(" (CORE)");
        }
        str.append("\n");
        for (auto& [className, klass] : lib->Image->Classes)
        {
            std::string adjustedClassName = className.empty() ? "<Functions Only>" : className;
            str.append("    Full Class Name: " + adjustedClassName + "\n");
            CapyClass* klassPtr = klass.get();
            for (auto& [name, sym] : klassPtr->SymbolMetaDatas)
            {
                str.append("      Symbol: " + sym.Signature + "\n");
                std::string symType = sym.IsVariable ? "Variable" : "Method";
                std::string adjustedSymName;
                if (!sym.Namespace.empty())
                    adjustedSymName.append(sym.Namespace + "::");
                if (!sym.ClassName.empty())
                    adjustedSymName.append(sym.ClassName + "::");

                adjustedSymName.append(sym.Name);
                str.append("        (" + symType + ") " + sym.ReturnType + " " + adjustedSymName);

                if (!sym.IsVariable)
                {
                    str.append("(");
                    bool first = true;
                    for (auto& param : sym.ParameterTypes)
                    {
                        if (!first) str.append(", ");
                        str.append(param);
                        first = false;
                    }
                    str.append(")");
                }
                str.append(";\n");
            }
        }
    }

    return str;

}

void capy_reload_libraries_into_domain(CapyDomain* cd)
{
    for (auto& entry : std::filesystem::directory_iterator(s_Storage.SearchPath))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".so")
        {
            capy_domain_library_open(cd, entry.path().filename(), false);
        }
    }
}

CapyLibrary* capy_domain_library_open(CapyDomain* d, const std::string& libName, bool isCore)
{
    std::filesystem::path libPath = s_Storage.SearchPath;
    libPath.append(libName);

    int fd = open(libPath.c_str(), O_RDONLY);
    if (fd < 0 && !s_Storage.SearchPath.empty())
    {
        fd = open(libName.c_str(), O_RDONLY);
        if (fd < 0)
        {
            std::cerr << "ERROR: failed to open file: " << libPath.generic_string() << "\n";
            return nullptr;
        }
        libPath = std::filesystem::path(libName);
    }



    if (d->Libraries.find(libPath.filename().string()) != d->Libraries.end())
    {
        return d->Libraries.at(libPath.filename().string()).get();
    }

    elf::elf ef(elf::create_mmap_loader(fd));
    dwarf::dwarf dw;
    try {
        dw = dwarf::dwarf(dwarf::elf::create_loader(ef));
    } catch (std::exception& e)
    {
        std::cerr << "ERROR: " << e.what() << "\n";
    }
    auto image = std::make_unique<CapyImage>();
    std::vector<SymbolMetaData> symbols;

    for (auto &cu : dw.compilation_units())
        traverse_and_collect(cu.root(), *(new std::vector<std::string>), s_Storage, symbols);

    close(fd);

    update_symbol_namespaces(symbols);

    symbols = process_library(ef, symbols, s_Storage);

    void* instance = dlopen(libPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!instance)
    {
        std::cerr << "ERROR: " << dlerror() << "\n";
        return nullptr;
    }

    std::unordered_map<std::string, std::unique_ptr<CapyClass>> classes;

    for (auto& sym : symbols)
    {
        void* handle = nullptr;
        if (!sym.Signature.empty())
            handle = dlsym(instance, sym.Signature.c_str());
        

        // Try internal calls first (highest priority)
        if (s_Storage.InternalCalls.contains(sym.Name))
        {
            handle = s_Storage.InternalCalls[sym.Name];
        }

        if (!handle && (!sym.IsVariable && !sym.IsClassInstance))
            continue;

        std::string fullName;
        if (!sym.Namespace.empty() && !sym.ClassName.empty())
            fullName += sym.Namespace + "::" + sym.ClassName;
        else if (!sym.Namespace.empty())
            fullName += sym.Namespace;
        else if (!sym.ClassName.empty())
            fullName += sym.ClassName;


        // Ensure class exists or create new
        CapyClass* klass = nullptr;
        auto it = classes.find(fullName);
        if (it == classes.end())
        {
            auto newKlass = std::make_unique<CapyClass>();
            newKlass->NameSpace = sym.Namespace;
            newKlass->ClassName = sym.ClassName;
            newKlass->VTable = std::make_unique<CapyVTable>();
            klass = newKlass.get();
            classes[fullName] = std::move(newKlass);
        }
        else
        {
            klass = it->second.get();
        }

        // Store symbol metadata
        klass->SymbolMetaDatas[sym.Name] = sym;

        if (sym.IsVariable)
        {
            auto field = std::make_unique<CapyField>();
            field->SymHandle = handle;
            field->FieldType = string_to_value_type(sym.ReturnType);
            field->FieldTypeString = sym.ReturnType;
            field->Offset = sym.Offset;
            field->ClassMember = sym.IsVariable && sym.IsClassInstance;
            klass->VTable->Fields[sym.Name] = std::move(field);
        }
        else
        {
            auto method = std::make_unique<CapyMethod>();
            method->SymHandle = handle;
            method->ReturnType = string_to_value_type(sym.ReturnType);

            if (sym.IsClassInstance)
            {
                method->ClassMember = true;
                method->Parameters.push_back(ValueType::POINTER);
            }
            for (auto& param : sym.ParameterTypes)
            {
                ValueType type = string_to_value_type(param);
                if (type != ValueType::VOID)
                    method->Parameters.push_back(type);
            }

            klass->VTable->Methods[sym.Name] = std::move(method);
        }
    }
    image->Classes = std::move(classes);

    std::unique_ptr<CapyLibrary> library = std::make_unique<CapyLibrary>(std::move(image));
    library->SymbolInstance = std::move(instance);
    library->IsCore = isCore;

    if (isCore)
        d->CoreLibraries.push_back(libPath.filename().c_str());


    d->Libraries[libPath.filename().c_str()] = std::move(library);

    return d->Libraries.at(libPath.filename().string()).get();
}

std::vector<std::string> capy_get_core_libraries_from_domain(const std::string &domainName)
{
    auto it = s_Storage.Domains.find(domainName);
    if (it == s_Storage.Domains.end())
    {
        std::cerr << "ERROR: domain '" << domainName << "' doesn't exists!\n";
        return {};
    }

    CapyDomain* domain = it->second.get();

    return domain->CoreLibraries;
}

CapyImage* capy_library_get_image(CapyLibrary* l)
{
    return l->Image.get();
}

CapyClass* capy_class_from_name(CapyImage* i, const std::string& nameSpace, const std::string& className)
{
    std::string fullName;
    if (!nameSpace.empty() && !className.empty())
        fullName += nameSpace + "::" + className;
    else if (!nameSpace.empty())
        fullName += nameSpace;
    else if (!className.empty())
        fullName += className;

    if (i->Classes.find(fullName) != i->Classes.end())
        return i->Classes.at(fullName).get();

    return nullptr;
}

CapyMethod* capy_method_from_class(CapyClass* c, const std::string& functionName)
{
    return c->VTable->Methods[functionName].get();
}

CapyField* capy_field_from_class(CapyClass* c, const std::string& fieldName)
{
    return c->VTable->Fields[fieldName].get();
}

void capy_field_data_get(void* instance, CapyClass* cc, const std::string& fieldName, void* value)
{
    CapyField* f = capy_field_from_class(cc, fieldName);

    if (!f) return;

    if (instance)
    {
        void* ptr = static_cast<char*>(instance) + f->Offset;
        memcpy(value, ptr, type_size(f->FieldType));
    }
    else
    {
        if (f->DefaultData.size() < type_size(f->FieldType))
            f->DefaultData.resize(type_size(f->FieldType));
        memcpy(value, f->DefaultData.data(), type_size(f->FieldType));
    }
}

void capy_field_data_get(void* instance, CapyField* cf, void* value)
{
    if (!cf) return;

    if (instance)
    {
        void* ptr = static_cast<char*>(instance) + cf->Offset;
        memcpy(value, ptr, type_size(cf->FieldType));
    }
    else
    {
        if (cf->DefaultData.size() < type_size(cf->FieldType))
            cf->DefaultData.resize(type_size(cf->FieldType));
        memcpy(value, cf->DefaultData.data(), type_size(cf->FieldType));
    }
}

void capy_field_data_set(void* instance, CapyClass* cc, const std::string& fieldName, void* value)
{
    CapyField* f = capy_field_from_class(cc, fieldName);
    if (!f) return;

    if (instance) {
        void* ptr = static_cast<char*>(instance) + f->Offset;
        memcpy(ptr, value, type_size(f->FieldType));
    } else {
        if (f->DefaultData.size() < type_size(f->FieldType))
            f->DefaultData.resize(type_size(f->FieldType));
        memcpy(f->DefaultData.data(), value, type_size(f->FieldType));
    }
}

void capy_field_data_set(void* instance, CapyField* cf, void* value, int valueSizeOverride)
{
    if (!cf) {
        fprintf(stderr, "capy_field_data_set: cf == nullptr\n");
        return;
    }

    // If you have owner class size in metadata, validate bounds:
    size_t size = type_size(cf->FieldType);

    FieldSetterFunc setter = nullptr;

    // Lookup setter for this type
    auto it = s_Storage.TypeSetters.find(cf->FieldTypeString);
    if (it != s_Storage.TypeSetters.end())
        setter = it->second;

    if (instance)
    {
        void* ptr = static_cast<char*>(instance) + cf->Offset;

        if (setter)
        {
            setter(ptr, value); // use registered type handler
        }
        else
        {
            memcpy(ptr, value, size); // default memcpy
        }
    }
    else
    {
        if (cf->DefaultData.size() < size)
            cf->DefaultData.resize(size);

        if (setter)
        {
            setter(cf->DefaultData.data(), value);
        }
        else
        {
            memcpy(cf->DefaultData.data(), value, size);
        }
    }
}

void* capy_function_call_from_method(CapyMethod* method, const std::vector<RuntimeValue>& values)
{
    if (!method || !method->SymHandle) return nullptr;

    if (values.size() != method->Parameters.size())
        throw std::runtime_error("Incorrect number of arguments provided!");

    size_t nargs = values.size();
    std::vector<ffi_type*> ffiArgTypes(nargs);
    std::vector<void*> ffiArgValues(nargs);
    std::vector<RuntimeValue> localValues = values;

    for (size_t i = 0; i < method->Parameters.size(); ++i) {
        ffiArgTypes[i] = get_ffi_type_p(method->Parameters[i]);
        ffiArgValues[i] = get_ffi_arg_p(localValues[i], method->Parameters[i]);
    }

    ffi_cif cif;
    ffi_type* ffiRet = get_ffi_type_p(method->ReturnType);


    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, nargs, ffiRet, ffiArgTypes.data()) != FFI_OK)
    {
        std::cerr << "ffi_prep_cif failed\n";
        return nullptr;
    }

    union {
        int16_t i16;
        int32_t i32;
        int64_t i64;
        uint16_t ui16;
        uint32_t ui32;
        uint64_t ui64;
        float f;
        void* p;
    } ret;

    // For member functions, reinterpret the pointer as a callable (void*, ...)
    if (method->ClassMember)
    {
        using MemberFn = void(*)(...);
        ffi_call(&cif, FFI_FN(reinterpret_cast<MemberFn>(method->SymHandle)), &ret, ffiArgValues.data());
    }
    else
    {
        ffi_call(&cif, FFI_FN(method->SymHandle), &ret, ffiArgValues.data());
    }

    switch(method->ReturnType)
    {
        case ValueType::INT16: return new int16_t(ret.i16);
        case ValueType::INT32: return new int32_t(ret.i32);
        case ValueType::INT64: return new int64_t(ret.i64);
        case ValueType::UINT16: return new uint16_t(ret.ui16);
        case ValueType::UINT32: return new uint32_t(ret.ui32);
        case ValueType::UINT64: return new uint64_t(ret.ui64);
        case ValueType::FLOAT: return new float(ret.f);
        case ValueType::POINTER: return ret.p;
        case ValueType::VOID: return nullptr;
    }

    return nullptr;
}

template<> constexpr const char* capy_type_name<float>() { return "Float"; }
template<> constexpr const char* capy_type_name<double>() { return "Double"; }
template<> constexpr const char* capy_type_name<int16_t>() { return "Int16"; }
template<> constexpr const char* capy_type_name<int32_t>() { return "Int32"; }
template<> constexpr const char* capy_type_name<int64_t>() { return "Int64"; }
template<> constexpr const char* capy_type_name<uint16_t>() { return "UInt16"; }
template<> constexpr const char* capy_type_name<uint32_t>() { return "UInt32"; }
template<> constexpr const char* capy_type_name<uint64_t>() { return "UInt64"; }



void capy_add_internal_call(const std::string& name, void* functionSymbol)
{
    if (s_Storage.InternalCalls.contains(name)) return;

    s_Storage.InternalCalls[name] = functionSymbol;

    
    for (auto& [_, domain] : s_Storage.Domains)
    {
        for (auto& [_, library] : domain->Libraries)
        {
            if (!library->SymbolInstance) continue;

            CapyImage* img = library->Image.get();
            for (auto& [_, cls] : img->Classes)
            {
                for (auto& [symName, sym] : cls->SymbolMetaDatas)
                {
                    if (symName == name)
                    {
                        // Directly patch the pointer in the plugin
                        void** addr = reinterpret_cast<void**>(cls->VTable->Fields[symName]->SymHandle);
                        if (addr)
                            *addr = functionSymbol;
                    }
                }
            }
        }
    }
}

void capy_add_type_setter(const std::string& name, FieldSetterFunc setter)
{
    s_Storage.TypeSetters[name] = setter;
}
