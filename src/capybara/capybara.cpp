#include "cpypch.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <ffi.h>

#include "capybara/capybara.h"
#include "capybara/runtime.h"

#include "fswatcher/fswatcher.h"

#include "util/libelf_util.h"
#include "util/ffi_util.h"

#include "util/fswatcher_utils.h"

RuntimeStorage s_Storage;

static void update_symbol_namespaces(std::vector<_SymbolMetaData>& symbols)
{
    for (auto& sym : symbols)
    {
        std::string& NameSpace = sym.Namespace;
        //std::cout << "NAMESPACE: " << NameSpace << "::" << sym.Name << "\n";
        for (auto& knownName : s_Storage.ConfigStorage.KnownClassNames)
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

static std::vector<std::string> s_IGNORED_NAMESPACES = { "std", "__gnu", "<anon>", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "_IO_", "_G_" };
static std::vector<std::string> s_IGNORED_CLASSNAMES = { "std", "__gnu", "<anon>", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "_IO_", "_G_", "__pthread", "timespec", "lconv", "_M_" };
static std::vector<std::string> s_IGNORED_NAMES = { "<anon>", "gp_offset", "fp_offset", "overflow_arg_area", "reg_save_area", "tm_", "_vptr." };

void capy_init()
{
    auto& cfg = s_Storage.ConfigStorage;
    if (cfg.IgnoredNamespaces.empty())
    {
        cfg.IgnoredNamespaces = s_IGNORED_NAMESPACES;
    }
    else
    {
        for (const auto& key : s_IGNORED_NAMESPACES)
            cfg.IgnoredNamespaces.push_back(key);
    }

    if (cfg.IgnoredClassNames.empty())
    {
        cfg.IgnoredClassNames = s_IGNORED_CLASSNAMES;
    }
    else
    {
        for (const auto& key : s_IGNORED_CLASSNAMES)
            cfg.IgnoredClassNames.push_back(key);
    }

    if (cfg.IgnoredNames.empty())
    {
        cfg.IgnoredNames = s_IGNORED_NAMES;
    }
    else
    {
        for (const auto& key : s_IGNORED_NAMES)
            cfg.IgnoredNames.push_back(key);
    }
}

void capy_shutdown()
{
    for (auto& [name, domain] : s_Storage.Storage.Domains)
    {
        CapyDomain* d = domain.get();
        capy_unload_domain(name);
    }

    for (auto& watcher : s_Storage.JITStorage.FileWatchers)
    {
        stop_watcher(*watcher);
    }

    s_Storage.JITStorage.FileWatchers.clear();
}

void capy_set_libraries_path(const std::filesystem::path &libPath)
{
    s_Storage.ConfigStorage.BinaryPath = libPath;
}

void capy_set_ignored_namespace(const std::vector<std::string>& ignoredNamespace)
{
    for (auto& nameSpace : ignoredNamespace)
        s_Storage.ConfigStorage.IgnoredNamespaces.push_back(nameSpace);
}

void capy_set_ignore_empty_namespace(bool active)
{
    s_Storage.ConfigStorage.IgnoreEmptyNamespaces = active;
}

void capy_set_ignored_classname(const std::vector<std::string>& ignoredClassName)
{
    for (auto& className : ignoredClassName)
        s_Storage.ConfigStorage.IgnoredClassNames.push_back(className);
}

CapyDomain* capy_init_domain(const std::string& name)
{
    auto& storage = s_Storage.Storage;
    uint32_t domainHash = generate_hash(name);
    if (storage.Domains.find(domainHash) != storage.Domains.end())
    {
        std::cerr << "ERROR: domain '" << name << "' already exists!\n";
        return nullptr;
    }

    std::unique_ptr<CapyDomain> domain = std::make_unique<CapyDomain>();
    CapyDomain* ptr = domain.get();

    storage.Domains[domainHash] = std::move(domain);

    return ptr;
}

void capy_unload_domain(const std::string& domainName)
{
    auto& storage = s_Storage.Storage;
    uint32_t domainHash = generate_hash(domainName);
    auto it = storage.Domains.find(domainHash);
    if (it == storage.Domains.end())
    {
        std::cout << "Domain: " << domainName << " doesn't exist!\n";
        return;
    }

    storage.Domains.erase(it);
    storage.InternalCalls.clear();
}

void capy_unload_domain(const uint32_t& domainHash)
{
    auto& storage = s_Storage.Storage;
    auto it = storage.Domains.find(domainHash);
    if (it == storage.Domains.end())
    {
        return;
    }

    storage.Domains.erase(it);
    storage.InternalCalls.clear();
}

std::string capy_dump_domain(const std::string& domainName)
{
    auto& storage = s_Storage.Storage;
    uint32_t domainHash = generate_hash(domainName);
    if (storage.Domains.find(domainHash) == storage.Domains.end())
    {
        std::cerr << "capy_dump_domain: Domain '" << domainName << "' doesn't exist!\n";
        return "<null>\n";
    }
    std::string str = "Domain: " + domainName + "\n";
    CapyDomain* domain = storage.Domains[domainHash].get();
    for (auto& [_, lib] : domain->Libraries)
    {
        str.append("  Library: " + lib->Name);
        if (lib->IsCore)
        {
            str.append(" (CORE)");
        }
        str.append("\n");
        for (auto& [_, klass] : lib->Image->Classes)
        {
            std::string adjustedClassName = klass->ClassName.empty() ? "<Functions Only>" : klass->ClassName;
            str.append("    Full Class Name: " + adjustedClassName + "\n");
            CapyClass* klassPtr = klass.get();
            for (auto& [hash, sym] : klassPtr->VTable->Methods)
            {
                str.append("      Method Symbol: " + sym->SymbolMetaData.Signature + "\n");
                std::string symType = "Method";
                std::string adjustedSymName;
                if (!sym->SymbolMetaData.Namespace.empty())
                    adjustedSymName.append(sym->SymbolMetaData.Namespace + "::");
                if (!sym->SymbolMetaData.ClassName.empty())
                    adjustedSymName.append(sym->SymbolMetaData.ClassName + "::");

                adjustedSymName.append(sym->SymbolMetaData.Name);
                str.append("        (" + symType + ") " + sym->SymbolMetaData.ReturnType + " " + adjustedSymName);
                str.append("(");
                bool first = true;
                for (auto& param : sym->SymbolMetaData.ParameterTypes)
                {
                    if (param.empty())
                        continue;

                    if (!first) str.append(", ");
                    str.append(param);
                    first = false;
                }
                str.append(");\n");
            }
            for (auto& [hash, sym] : klassPtr->VTable->Fields)
            {
                str.append("      Field Symbol: " + sym->SymbolMetaData.Signature + "\n");
                std::string symType = "Field";
                std::string adjustedSymName;
                if (!sym->SymbolMetaData.Namespace.empty())
                    adjustedSymName.append(sym->SymbolMetaData.Namespace + "::");
                if (!sym->SymbolMetaData.ClassName.empty())
                    adjustedSymName.append(sym->SymbolMetaData.ClassName + "::");

                adjustedSymName.append(sym->SymbolMetaData.Name);
                str.append("        (" + symType + ") " + sym->SymbolMetaData.ReturnType + " " + adjustedSymName + ";\n");
            }
        }
    }

    return str;

}

void capy_reload_libraries_into_domain(CapyDomain* cd)
{
    for (auto& entry : std::filesystem::directory_iterator(s_Storage.ConfigStorage.BinaryPath))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".so")
        {
            capy_domain_library_open(cd, entry.path().filename(), false);
        }
    }
}

CapyLibrary* capy_domain_library_open(CapyDomain* d, const std::string& libName, bool isCore)
{
    std::filesystem::path libPath = s_Storage.ConfigStorage.BinaryPath;
    libPath.append(libName);

    int fd = open(libPath.c_str(), O_RDONLY);
    if (fd < 0 && !s_Storage.ConfigStorage.BinaryPath.empty())
    {
        fd = open(libName.c_str(), O_RDONLY);
        if (fd < 0)
        {
            std::cerr << "FILE: failed to open file: " << libName << "\n";
            return nullptr;
        }
        libPath = std::filesystem::path(libName);
    }
    else if (fd < 0)
    {
        std::cerr << "FILE ERROR: failed to open file: " << libPath.generic_string() << "\n";
        return nullptr;
    }

    uint32_t libHash = generate_hash(libPath.filename().string());

    if (d->Libraries.find(libHash) != d->Libraries.end())
    {
        return d->Libraries.at(libHash).get();
    }

    auto& storage = s_Storage.Storage;

    elf::elf ef(elf::create_mmap_loader(fd));
    dwarf::dwarf dw;
    try {
        dw = dwarf::dwarf(dwarf::elf::create_loader(ef));
    } catch (std::exception& e)
    {
        std::cerr << "ERROR: " << e.what() << "\n";
    }
    auto image = std::make_unique<CapyImage>();
    std::vector<_SymbolMetaData> symbols;

    for (auto &cu : dw.compilation_units())
        traverse_and_collect(cu.root(), *(new std::vector<std::string>), s_Storage, symbols);

    close(fd);

    update_symbol_namespaces(symbols);

    symbols = process_library(ef, symbols, storage);

    void* instance = dlopen(libPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!instance)
    {
        std::cerr << "DLOPEN ERROR: " << dlerror() << "\n";
        return nullptr;
    }

    std::unordered_map<uint32_t, std::unique_ptr<CapyClass>> classes;

    for (auto& sym : symbols)
    {
        void* handle = nullptr;
        if (!sym.Signature.empty())
            handle = dlsym(instance, sym.Signature.c_str());
        
        uint32_t callHash = generate_hash(sym.Name);

        // Try internal calls first (highest priority)
        if (s_Storage.Storage.InternalCalls.contains(callHash))
        {
            void** handleAddr = reinterpret_cast<void**>(handle);
            *handleAddr = s_Storage.Storage.InternalCalls[callHash];
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
        uint32_t classHash = generate_hash(fullName);
        auto it = classes.find(classHash);
        if (it == classes.end())
        {
            auto newKlass = std::make_unique<CapyClass>();
            newKlass->NameSpace = sym.Namespace;
            newKlass->ClassName = sym.ClassName;
            newKlass->VTable = std::make_unique<CapyVTable>();
            klass = newKlass.get();
            classes[classHash] = std::move(newKlass);
        }
        else
        {
            klass = it->second.get();
        }

        if (sym.IsVariable)
        {
            auto field = std::make_unique<CapyField>();
            field->SymHandle = handle;
            field->FieldType = string_to_value_type(sym.ReturnType);
            field->FieldTypeString = sym.ReturnType;
            field->Offset = sym.Offset;
            field->ClassMember = sym.IsVariable && sym.IsClassInstance;
            field->SymbolMetaData = sym;
            uint32_t fieldHash = generate_hash(sym.Name);
            klass->VTable->Fields[fieldHash] = std::move(field);
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

            method->SymbolMetaData = sym;
            uint32_t methodHash = generate_hash(sym.Name);
            klass->VTable->Methods[methodHash] = std::move(method);
        }
    }
    image->Classes = std::move(classes);

    std::unique_ptr<CapyLibrary> library = std::make_unique<CapyLibrary>(std::move(image));
    library->SymbolInstance = std::move(instance);
    library->IsCore = isCore;

    if (isCore)
    {
        d->CoreLibraries.push_back(libPath.filename().c_str());
    }

    d->Libraries[libHash] = std::move(library);

    return d->Libraries.at(libHash).get();
}

std::vector<std::string> capy_get_core_libraries_from_domain(const std::string &domainName)
{
    auto& storage = s_Storage.Storage;
    uint32_t domainHash = generate_hash(domainName);
    auto it = storage.Domains.find(domainHash);
    if (it == storage.Domains.end())
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

    uint32_t classHash = generate_hash(fullName);
    if (i->Classes.find(classHash) != i->Classes.end())
        return i->Classes.at(classHash).get();

    return nullptr;
}

CapyMethod* capy_method_from_class(CapyClass* c, const std::string& functionName)
{
    uint32_t funcHash = generate_hash(functionName);
    return c->VTable->Methods[funcHash].get();
}

CapyField* capy_field_from_class(CapyClass* c, const std::string& fieldName)
{
    uint32_t fieldHash = generate_hash(fieldName);
    return c->VTable->Fields[fieldHash].get();
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
        memcpy(value, f->DefaultData.raw_ptr(), type_size(f->FieldType));
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
        memcpy(value, cf->DefaultData.raw_ptr(), type_size(cf->FieldType));
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
        memcpy(f->DefaultData.raw_ptr(), value, type_size(f->FieldType));
    }
}

void capy_field_data_set(void* instance, CapyField* cf, void* value, int valueSizeOverride)
{
    auto& storage = s_Storage.Storage;
    if (!cf) {
        fprintf(stderr, "capy_field_data_set: cf == nullptr\n");
        return;
    }

    // If you have owner class size in metadata, validate bounds:
    size_t size = type_size(cf->FieldType);

    FieldSetterFunc setter = nullptr;

    // Lookup setter for this type
    uint32_t fieldSetterHash = generate_hash(cf->FieldTypeString);
    auto it = storage.TypeSetters.find(fieldSetterHash);
    if (it != storage.TypeSetters.end())
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
        if (setter)
        {
            setter(cf->DefaultData.raw_ptr(), value);
        }
        else
        {
            memcpy(cf->DefaultData.raw_ptr(), value, size);
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
        ffiArgValues[i] = get_ffi_arg_p(localValues[i]);
    }

    ffi_cif cif;
    ffi_type* ffiRet = get_ffi_type_p(method->ReturnType);


    if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, nargs, ffiRet, ffiArgTypes.data()) != FFI_OK)
    {
        std::cerr << "ffi_prep_cif failed\n";
        return nullptr;
    }

    void* ret = nullptr;

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
    return ret;

}

void capy_add_internal_call(const std::string& name, void* functionSymbol)
{
    auto& storage = s_Storage.Storage;
    uint32_t callHash = generate_hash(name);
    if (storage.InternalCalls.contains(callHash)) return;

    storage.InternalCalls[callHash] = functionSymbol;

    
    for (auto& [_, domain] : storage.Domains)
    {
        for (auto& [_, library] : domain->Libraries)
        {
            if (!library->SymbolInstance) continue;

            CapyImage* img = library->Image.get();
            for (auto& [_, cls] : img->Classes)
            {
                for (auto& [symHash, sym] : cls->VTable->Fields)
                {
                    uint32_t nameHash = generate_hash(name);
                    if (symHash == nameHash)
                    {
                        // Directly patch the pointer in the plugin
                        void** addr = reinterpret_cast<void**>(cls->VTable->Fields[symHash]->SymHandle);
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
    uint32_t setterHash = generate_hash(name);
    s_Storage.Storage.TypeSetters[setterHash] = setter;
}
