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
        for (auto& knownName : s_Storage.Config.KnownClassNames)
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

static std::string jit_get_compile_command(const std::filesystem::path& filePath)
{
    auto& jit = s_Storage.Active.JITStorage;
    std::string coreLibLinks;
    for (auto& lib : s_Storage.Active.Runtime->CoreLibraries)
    {
        std::string libFix;
        std::string libCheck = lib.substr(0, 3);
        if (libCheck == "lib")
            libFix = lib.substr(3);
        else 
            libFix = lib;

        libFix = libFix.substr(0, libFix.length() - 3);

        coreLibLinks.append("-l" + libFix + " ");
    }
    
    std::filesystem::path rootDir = std::filesystem::current_path();

    std::filesystem::path compilePath = rootDir / s_Storage.Config.BinaryPath / filePath.filename();
    compilePath.replace_extension(".so");

    std::filesystem::path sourceFile = rootDir / filePath;
    sourceFile.replace_extension(".cpp");

    std::stringstream ss;
    ss << "gcc " << sourceFile << " -o " << compilePath << " \\\n";
    if (!jit.CorePath.empty())
    {
        ss << "-I" << jit.CorePath.string() << " \\\n";
    }
    if (jit.CoreBinaryPaths.size() > 0)
    {
        for (auto& path : jit.CoreBinaryPaths)
        {
            std::filesystem::path truePath = rootDir / path;
            ss << "-L" << truePath.parent_path().string() << " ";
        }
        ss << "\\\n";
    }
    ss << coreLibLinks << "\\\n"
       << "-fPIC -shared -lstdc++ -gdwarf-4\n";
    return ss.str();
}

static void jit_worker()
{
    auto& jit = s_Storage.Active.JITStorage;
    while (jit.JitRunning)
    {
        bool hasWork = false;

        {
            std::lock_guard<std::mutex> lock(jit.JitMutex);

            if (!jit.WatcherStorage.UpdatedFiles.empty())
            {
                jit.FilesToCompile.swap(jit.WatcherStorage.UpdatedFiles);
                hasWork = true;
            }
        } 

        if (hasWork)
        {
            for (auto& path : jit.FilesToCompile)
            {
                jit.CompilationCommands.push_back(jit_get_compile_command(path));
            }

            jit.JitCompilationNeeded = true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}


CapyDomain* capy_jit_init()
{
    auto& cfg = s_Storage.Config;
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

    auto& jit = s_Storage.Active.JITStorage;

    // Create the Domain
    CapyDomain* cd = new CapyDomain;

    s_Storage.Active.Runtime.reset(cd);

    jit.JitRunning = true;
    jit.JitThread = std::thread(jit_worker);

    fswatcher_start_storage(jit.WatcherStorage);

    return cd;
}

void capy_jit_shutdown()
{
    auto& active = s_Storage.Active;

    auto& jit = s_Storage.Active.JITStorage;

    if (jit.JitRunning.exchange(false))
    {
        if (jit.JitThread.joinable())
            jit.JitThread.join();
    }

    fswatcher_stop_storage(jit.WatcherStorage);

    jit.WatcherStorage.Watchers.clear();
    {
        std::lock_guard<std::mutex> lock(jit.JitMutex);
        jit.PendingFiles.clear();
        jit.FilesToCompile.clear();
        jit.CompilationCommands.clear();
    }

    jit.JitCompilationNeeded.store(false);

    if (active.Runtime)
    {
        active.Runtime.reset();
    }

    active.InternalCalls.clear();
    active.TypeSetters.clear();
}

bool capy_jit_poll()
{
    std::filesystem::path rootDir = std::filesystem::current_path();
    auto& jit = s_Storage.Active.JITStorage;
    if (jit.JitCompilationNeeded.exchange(false))
    {
        std::vector<std::filesystem::path> files_being_compiled;

        {
            std::lock_guard<std::mutex> lock(jit.JitMutex);
            files_being_compiled.swap(jit.FilesToCompile); // move pending files
        }

        std::vector<std::string> commands;

        {
            std::lock_guard<std::mutex> lock(jit.JitMutex);
            commands.swap(jit.CompilationCommands);
        }

        for (auto& command : commands)
        {
            system(command.c_str());
        }

        for (auto& path : files_being_compiled)
        {
            std::filesystem::path soPath = rootDir / s_Storage.Config.BinaryPath / path.filename();
            soPath.replace_extension(".so");
            while (!std::filesystem::exists(soPath))
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        capy_reload_domain();



        CapyDomain* raw = new CapyDomain;
        s_Storage.Active.Runtime.reset(raw);

        for (auto& lib : jit.CoreBinaryPaths)
        {
            capy_domain_core_library_open(lib);
        }

        capy_reload_libraries_into_domain();
        return true;
    }
    return false;
}

void capy_jit_set_binary_path(const std::filesystem::path &binaryPath)
{
    s_Storage.Config.BinaryPath = binaryPath;
}

void capy_jit_set_core_bin_include_path(const std::filesystem::path &includePath)
{
    s_Storage.Active.JITStorage.CorePath = includePath;
}

void capy_jit_set_source_path(const std::filesystem::path& sourcePath, bool recursive)
{
    auto& jit = s_Storage.Active.JITStorage;
    fswatcher_add_watcher(jit.WatcherStorage, sourcePath, recursive);
}

void capy_set_ignored_namespace(const std::vector<std::string>& ignoredNamespace)
{
    for (auto& nameSpace : ignoredNamespace)
        s_Storage.Config.IgnoredNamespaces.push_back(nameSpace);
}

void capy_set_ignore_empty_namespace(bool active)
{
    s_Storage.Config.IgnoreEmptyNamespaces = active;
}

void capy_set_ignored_classname(const std::vector<std::string>& ignoredClassName)
{
    for (auto& className : ignoredClassName)
        s_Storage.Config.IgnoredClassNames.push_back(className);
}

void capy_reload_domain()
{
    auto& active = s_Storage.Active;
    auto& jit = active.JITStorage;

    /* 1. Stop JIT */
    if (jit.JitRunning.exchange(false))
    {
        if (jit.JitThread.joinable())
            jit.JitThread.join();
    }

    /* 2. Destroy domain */
    active.Runtime.reset();

    /* 3. Create new domain */
    CapyDomain* newDomain = new CapyDomain;
    active.Runtime.reset(newDomain);

    /* 4. Restart JIT */
    jit.JitRunning.store(true);
    jit.JitThread = std::thread(jit_worker);
}

std::string capy_dump_domain()
{
    auto* cd = s_Storage.Active.Runtime.get();

    if (!cd)
    {
        return "Domain not set!";
    }

    std::string str;
    for (auto& [_, lib] : cd->Libraries)
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

void capy_reload_libraries_into_domain()
{
    auto* cd = s_Storage.Active.Runtime.get();

    if (!cd)
    {
        std::cerr << "ERROR: Domain not set!\nBe sure to call capy_jit_init!\n";
        return;
    }

    if (s_Storage.Config.BinaryPath.empty())
        s_Storage.Config.BinaryPath = std::filesystem::current_path();

    for (auto& entry : std::filesystem::directory_iterator(s_Storage.Config.BinaryPath))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".so")
        {
            capy_domain_library_open(entry.path().filename());
        }
    }
}

CapyLibrary* capy_domain_library_open(const std::string& binName)
{
    auto* cd = s_Storage.Active.Runtime.get();

    if (!cd)
    {
        std::cerr << "ERROR: Domain not set!\nBe sure to call capy_jit_init!\n";
        return nullptr;
    }


    if (s_Storage.Config.BinaryPath.empty())
        s_Storage.Config.BinaryPath = std::filesystem::current_path();


    std::filesystem::path fullPath = s_Storage.Config.BinaryPath / binName;
    int fd = open(fullPath.c_str(), O_RDONLY);
    if (fd < 0)
    {
        std::cerr << "ERROR: Core library filed to open at: " << fullPath.string() << "\n";
        close(fd);
        return nullptr;
    }

    uint32_t libHash = generate_hash(fullPath.filename().string());

    if (cd->Libraries.find(libHash) != cd->Libraries.end())
    {
        return cd->Libraries.at(libHash).get();
    }

    auto& storage = s_Storage.Active;

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

    void* instance = dlopen(fullPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
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
        if (s_Storage.Active.InternalCalls.contains(callHash))
        {
            void** handleAddr = reinterpret_cast<void**>(handle);
            *handleAddr = s_Storage.Active.InternalCalls[callHash];
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
    library->IsCore = false;

    cd->Libraries[libHash] = std::move(library);

    return cd->Libraries.at(libHash).get();
}

CapyLibrary* capy_domain_core_library_open(const std::filesystem::path& binPath)
{
    auto* cd = s_Storage.Active.Runtime.get();

    if (!cd)
    {
        std::cerr << "ERROR: Domain not set!\nBe sure to call capy_jit_init!\n";
        return nullptr;
    }



    int fd = open(binPath.c_str(), O_RDONLY);
    if (fd < 0)
    {
        std::cerr << "ERROR: Core library filed to open at: " << binPath.string() << "\n";
        close(fd);
        return nullptr;
    }


    uint32_t libHash = generate_hash(binPath.filename().string());

    if (cd->Libraries.find(libHash) != cd->Libraries.end())
    {
        return cd->Libraries.at(libHash).get();
    }

    auto& storage = s_Storage.Active;

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

    void* instance = dlopen(binPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
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
        if (s_Storage.Active.InternalCalls.contains(callHash))
        {
            void** handleAddr = reinterpret_cast<void**>(handle);
            *handleAddr = s_Storage.Active.InternalCalls[callHash];
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
    library->IsCore = true;

    auto& binaryPaths = s_Storage.Active.JITStorage.CoreBinaryPaths;
    cd->CoreLibraries.push_back(binPath.filename().c_str());
    bool found = false;
    for (auto& path : binaryPaths)
    {
        if (path == binPath)
        {
            found = true;
            break;
        }
    }

    if (!found)
        binaryPaths.push_back(binPath);

    cd->Libraries[libHash] = std::move(library);

    return cd->Libraries.at(libHash).get();
}

std::vector<std::string> capy_get_core_libraries_from_domain()
{
    auto* cd = s_Storage.Active.Runtime.get();

    if (!cd)
    {
        std::cerr << "ERROR: Domain not set!\nBe sure to call capy_jit_init!\n";
        return {};
    }

    if (!cd)
        return {};

    return cd->CoreLibraries;
}

CapyImage* capy_library_get_image(CapyLibrary* l)
{
    if (!l)
        return nullptr;

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
    auto* cd = s_Storage.Active.Runtime.get();

    if (!cd)
    {
        std::cerr << "ERROR: Domain not set!\nBe sure to call capy_jit_init!\n";
        return;
    }


    auto& storage = s_Storage.Active;
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
    auto* cd = s_Storage.Active.Runtime.get();

    if (!cd)
    {
        std::cerr << "ERROR: Domain not set!\nBe sure to call capy_jit_init!\n";
        return nullptr;
    }

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
    auto* cd = s_Storage.Active.Runtime.get();

    auto& storage = s_Storage.Active;
    uint32_t callHash = generate_hash(name);
    if (storage.InternalCalls.contains(callHash)) return;

    storage.InternalCalls[callHash] = functionSymbol;

    if (!cd)
        return;

    for (auto& [_, library] : cd->Libraries)
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


void capy_add_type_setter(const std::string& name, FieldSetterFunc setter)
{
    uint32_t setterHash = generate_hash(name);
    s_Storage.Active.TypeSetters[setterHash] = setter;
}

void capy_jit_set_fs_event_callback(FileEventCallback callback)
{
    s_Storage.Active.JITStorage.WatcherStorage.EventCallback = callback;
}

CapyDomain* capy_get_root_domain()
{
    auto* cd = s_Storage.Active.Runtime.get();
    if (!cd)
    {
        std::cerr << "ERROR: Domain not set!\nBe sure to call capy_jit_init!\n";
        return nullptr;
    }

    return cd;
}

void capy_jit_update_fs_event_watcher()
{
    fswatcher_update_file_events(s_Storage.Active.JITStorage.WatcherStorage);
}

void capy_jit_set_ignore_hidden_paths(bool active)
{
    s_Storage.Active.JITStorage.WatcherStorage.IgnoreHiddenPaths = active;
}
