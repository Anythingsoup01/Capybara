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

#include <signal.h>

RuntimeStorage g_Storage;

static void update_symbol_namespaces(std::vector<_SymbolMetaData>& symbols)
{
    for (auto& sym : symbols)
    {
        std::string& NameSpace = sym.Namespace;
        //std::cout << "NAMESPACE: " << NameSpace << "::" << sym.Name << "\n";
        for (auto& knownName : g_Storage.Config.KnownClassNames)
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
                sym.IsStruct = false;
                break;
            }
        }

        for (auto& knownName : g_Storage.Config.KnownStructNames)
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
                sym.IsStruct = true;
                break;
            }
        }}
}

static std::vector<std::string> s_IGNORED_NAMESPACES = { "std", "__gnu", "<anon>", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "_IO_", "_G_" };
static std::vector<std::string> s_IGNORED_CLASSNAMES = { "std", "__gnu", "<anon>", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "_IO_", "_G_", "__pthread", "timespec", "lconv", "_M_", "tm", "typedef __va_" };
static std::vector<std::string> s_IGNORED_NAMES = { "<anon>", "gp_offset", "fp_offset", "overflow_arg_area", "reg_save_area", "tm_", "__", "_IO", "_flags", "quot", "_markers", "_chain", "_short", "_old_offset", "_cur_column", "_vtable_offset", "_shortbuf", "_lock", "_offset", "_codecvt", "_wide_data", "_freeres", "_prevchain", "_mode", "_unused", "_total_written", "decimal_point", "thousands_sep", "goruping", "int_curr_symbol", "currency_symbol", "mon_decimal_point", "mon_thousands_sep", "mon_grouping", "positive_sign", "negative_sign", "int_frac_digits", "frac_digits", "p_cs_precedes", "p_sep_by_space", "n_cs_", "n_sep_", "p_sign_", "n_sign_", "int_p_", "int_n_", "rem", "_fileno", "grouping" };

static std::string jit_get_compile_command(const std::filesystem::path& filePath)
{
    auto& jit = g_Storage.Active.JITStorage;
    std::string coreLibLinks;
    for (auto& lib : g_Storage.Active.Runtime->CoreLibraries)
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

    std::filesystem::path compilePath = rootDir / g_Storage.Config.BinaryPath / filePath.filename();
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
    auto& jit = g_Storage.Active.JITStorage;
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
    auto& cfg = g_Storage.Config;
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

    auto& jit = g_Storage.Active.JITStorage;

    // Create the Domain
    CapyDomain* cd = new CapyDomain;

    g_Storage.Active.Runtime.reset(cd);

    jit.JitRunning = true;
    jit.JitThread = std::thread(jit_worker);

    fswatcher_start_storage(jit.WatcherStorage);

    return cd;
}

void capy_jit_shutdown()
{
    auto& active = g_Storage.Active;

    auto& jit = g_Storage.Active.JITStorage;

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
}

bool capy_jit_poll()
{
    std::filesystem::path rootDir = std::filesystem::current_path();
    auto& jit = g_Storage.Active.JITStorage;
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
            std::filesystem::path soPath = rootDir / g_Storage.Config.BinaryPath / path.filename();
            soPath.replace_extension(".so");
            while (!std::filesystem::exists(soPath))
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        capy_reload_domain();

        for (auto& coreLib : jit.CoreBinaryPaths)
        {
            capy_domain_library_open(coreLib, true);
        }

        capy_reload_libraries_into_domain();



        return true;
    }
    return false;
}

void capy_jit_set_binary_path(const std::filesystem::path &binaryPath)
{
    g_Storage.Config.BinaryPath = binaryPath;
}

void capy_jit_set_core_bin_include_path(const std::filesystem::path &includePath)
{
    g_Storage.Active.JITStorage.CorePath = includePath;
}

void capy_jit_set_source_path(const std::filesystem::path& sourcePath, FileEventCallback callback, bool recursive)
{
    auto& jit = g_Storage.Active.JITStorage;
    fswatcher_add_watcher(jit.WatcherStorage, sourcePath, recursive);
    jit.WatcherStorage.EventCallback = callback;
}

void capy_set_ignored_namespace(const std::vector<std::string>& ignoredNamespace)
{
    for (auto& nameSpace : ignoredNamespace)
        g_Storage.Config.IgnoredNamespaces.push_back(nameSpace);
}

void capy_set_ignore_empty_namespace(bool active)
{
    g_Storage.Config.IgnoreEmptyNamespaces = active;
}

void capy_set_ignored_classname(const std::vector<std::string>& ignoredClassName)
{
    for (auto& className : ignoredClassName)
        g_Storage.Config.IgnoredClassNames.push_back(className);
}

void capy_reload_domain()
{
    auto& active = g_Storage.Active;
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

    CapyDomain* raw = new CapyDomain;
    g_Storage.Active.Runtime.reset(raw);
}

static std::string dump_class(CapyClass* klass, bool isStruct)
{
    std::string str;
    std::string adjustedClassType = isStruct ? "  Full Struct Name: " : "  Full Class Name: ";
    std::string adjustedClassName = klass->ClassName.empty() ? "<Functions Only>" : klass->ClassName;
    str.append(adjustedClassType + adjustedClassName + " Declared Size: " + std::to_string(klass->ClassSize));
    if (klass->BaseClassSize > 0) str.append(" Base Class Size: " + std::to_string(klass->BaseClassSize));
    str.append("\n");
    for (auto& [hash, sym] : klass->VTable->Methods)
    {
        str.append("    Method Symbol: " + sym->SymbolMetaData.Signature + "\n");
        std::string symType = "Method";
        std::string adjustedSymName;
        if (!sym->SymbolMetaData.Namespace.empty())
            adjustedSymName.append(sym->SymbolMetaData.Namespace + "::");
        if (!sym->SymbolMetaData.ClassName.empty())
            adjustedSymName.append(sym->SymbolMetaData.ClassName + "::");

        adjustedSymName.append(sym->SymbolMetaData.Name);
        str.append("      (" + symType + ") " + sym->SymbolMetaData.ReturnType + " " + adjustedSymName);
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
    for (auto& [hash, sym] : klass->VTable->Fields)
    {
        str.append("    Field Symbol: " + sym->SymbolMetaData.Signature + "\n");
        std::string symType = "Field";
        std::string adjustedSymName;
        if (!sym->SymbolMetaData.Namespace.empty())
            adjustedSymName.append(sym->SymbolMetaData.Namespace + "::");
        if (!sym->SymbolMetaData.ClassName.empty())
            adjustedSymName.append(sym->SymbolMetaData.ClassName + "::");

        adjustedSymName.append(sym->SymbolMetaData.Name);
        str.append("      (" + symType + ") " + sym->SymbolMetaData.ReturnType + " " + adjustedSymName + " OFFSET (" + std::to_string(sym->Offset) + ");\n");
    }

    return str;
}

std::string capy_dump_domain()
{
    auto* cd = g_Storage.Active.Runtime.get();

    if (!cd)
    {
        return "Domain not set!";
    }

    std::string str;
    for (auto& [_, lib] : cd->Libraries)
    {
        str.append("Library: " + lib->Name);
        if (lib->IsCore)
        {
            str.append(" (CORE)");
        }
        str.append("\n");
        for (auto& [_, klass] : lib->Image->Classes)
        {
            str.append(dump_class(klass.get(), false));
        }
        for (auto& [_, klass] : lib->Image->Structures)
        {
            str.append(dump_class(klass.get(), true));
        }
    }

    return str;
}

void capy_register_class_or_struct_size(uint32_t hash, uint64_t size)
{
    g_Storage.Active.Runtime->StoredSizes[hash] = size;
}

uint64_t capy_get_class_or_struct_size(const std::string& nameSpace, const std::string& className)
{
    std::string fullName;
    if (!nameSpace.empty() && !className.empty())
        fullName += nameSpace + "::" + className;
    else if (!nameSpace.empty())
        fullName = nameSpace;
    else if (!className.empty())
        fullName = className;

    uint32_t classHash = generate_hash(fullName);

    if (!g_Storage.Active.Runtime->StoredSizes.contains(classHash))
        return 0;

    return g_Storage.Active.Runtime->StoredSizes[generate_hash(fullName)];
}

std::vector<CapyField*> capy_sort_and_set_fields_offset_from_class(CapyClass* klass)
{
    std::vector<CapyField*> allFields;
    for (auto& [_, field] : klass->VTable->Fields)
    {
        allFields.push_back(field.get());
    }

    std::vector<CapyField*> sortedFields;
    size_t actualOffset = 0;
    while(!allFields.empty())
    {
        
        int lowestOffsetIndex = 0;
        int lowestOffsetAmount = 0;

        bool firstRun = true;
        
        for (size_t i = 0; i < allFields.size(); ++i)
        {
            // If it's the first run, we just set the values
            // so we have a starting amount
            if (firstRun)
            {
                lowestOffsetAmount = allFields[i]->Offset;
                lowestOffsetIndex = i;
                firstRun = false;
                continue;
            }


            if (allFields[i]->Offset < lowestOffsetAmount)
            {
                lowestOffsetAmount = allFields[i]->Offset;
                lowestOffsetIndex = i;
            }
        }


        allFields[lowestOffsetIndex]->Offset = actualOffset;

        if (allFields[lowestOffsetIndex]->Size == 0)
        {
            auto* obtainedClass = capy_class_from_name(allFields[lowestOffsetIndex]->SymbolMetaData.Namespace, allFields[lowestOffsetIndex]->SymbolMetaData.ReturnType);
            if (obtainedClass)
            {
                actualOffset += obtainedClass->ClassSize;
            }
        }
        else
        {
            actualOffset += allFields[lowestOffsetIndex]->Size;
        }

        sortedFields.push_back(allFields[lowestOffsetIndex]);
        allFields.erase(allFields.begin() + lowestOffsetIndex);
    }

    return sortedFields;
}


void capy_reload_libraries_into_domain()
{
    auto* cd = g_Storage.Active.Runtime.get();

    if (!cd)
    {
        std::cerr << "ERROR: Domain not set!\nBe sure to call capy_jit_init!\n";
        return;
    }

    if (g_Storage.Config.BinaryPath.empty())
        g_Storage.Config.BinaryPath = std::filesystem::current_path();

    for (auto& entry : std::filesystem::directory_iterator(g_Storage.Config.BinaryPath))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".so")
        {
            capy_domain_library_open(entry.path().filename(), false);
        }
    }



    for (auto& [_, lib] : cd->Libraries)
    {
        for (auto& [klassHash, klass] : lib->Image->Structures)
        {
            uint64_t totalSize = 0;
            for (auto& [_, field] : klass->VTable->Fields)
                totalSize += field->Size;

            klass->ClassSize = totalSize;
            capy_register_class_or_struct_size(klassHash, totalSize);
        }
    }



    for (auto& [_, lib] : cd->Libraries)
    {
        for (auto& [klassHash, klass] : lib->Image->Classes)
        {
            uint64_t totalSize = 0;
            for (auto& [_, field] : klass->VTable->Fields)
            {
                if (field->Size == 0)
                {
                    uint64_t fieldSize = capy_get_class_or_struct_size(field->SymbolMetaData.Namespace, field->SymbolMetaData.ReturnType);
                    totalSize += fieldSize;
                    field->Size = fieldSize;
                }
                else
                {
                    totalSize += field->Size;
                }

            }
            klass->ClassSize = totalSize;
            capy_register_class_or_struct_size(klassHash, totalSize);
        }
    }

    for (auto& [_, lib] : cd->Libraries)
    {
        for (auto& [klassHash, klass] : lib->Image->Classes)
        {
            size_t totalSize = 0;

            for (auto& baseClass : g_Storage.Config.ClassMap[klassHash])
            {
                uint64_t fieldSize =  capy_get_class_or_struct_size(baseClass.NameSpace, baseClass.ClassName);
                totalSize += fieldSize;
            }

            klass->BaseClassSize = totalSize;

            for (auto& baseMeta : g_Storage.Config.ClassMap[klassHash])
            {
                CapyClass* baseClass = capy_class_from_name(baseMeta.NameSpace, baseMeta.ClassName);
                if (!baseClass) continue;

                for (auto& [fieldHash, baseField] : baseClass->VTable->Fields)
                {
                    // Avoid overwriting derived fields if same name exists
                    if (klass->VTable->Fields.contains(fieldHash)) continue;

                    // Copy base field into derived
                    auto copiedField = std::make_unique<CapyField>(*baseField);
                    klass->VTable->Fields[fieldHash] = std::move(copiedField);
                }
            }

        }
    }

    for (auto& [_, lib] : cd->Libraries)
    {
        for (auto& [_, klass] : lib->Image->Classes)
        {
            std::vector<CapyField*> sortedFields = capy_sort_and_set_fields_offset_from_class(klass.get());

            for (auto* field : sortedFields)
            {
                std::string fullName;
                if (!field->SymbolMetaData.Namespace.empty() && !field->SymbolMetaData.ClassName.empty())
                    fullName = field->SymbolMetaData.Namespace + "::" + field->SymbolMetaData.ClassName;
                else if (!field->SymbolMetaData.Namespace.empty())
                    fullName = field->SymbolMetaData.Namespace;
                if (field->SymbolMetaData.ClassName.empty())
                    fullName = field->SymbolMetaData.ClassName;
                fullName += "::" + field->SymbolMetaData.Name;

                uint32_t fieldHash = generate_hash(fullName);
                klass->VTable->Fields[fieldHash] = std::make_unique<CapyField>(*field);
            }
        }
    }

    for (auto& [_, lib] : cd->Libraries)
    {
        for (auto& [klassHash, klass] : lib->Image->Classes)
        {
            uint64_t totalSize = 0;
            for (auto& [_, field] : klass->VTable->Fields)
            {
                if (field->Size == 0)
                {
                    uint64_t fieldSize = capy_get_class_or_struct_size(field->SymbolMetaData.Namespace, field->SymbolMetaData.ReturnType);
                    totalSize += fieldSize;
                    field->Size = fieldSize;
                }
                else
                {
                    totalSize += field->Size;
                }

            }
            klass->ClassSize = totalSize;
        }
    }

}

CapyLibrary* capy_domain_library_open(const std::string& binName, bool isCore)
{
    auto* cd = g_Storage.Active.Runtime.get();

    if (!cd)
    {
        std::cerr << "ERROR: Domain not set!\nBe sure to call capy_jit_init!\n";
        return nullptr;
    }

    if (g_Storage.Config.BinaryPath.empty())
        g_Storage.Config.BinaryPath = std::filesystem::current_path();


    std::filesystem::path fullPath;
    if (isCore)
        fullPath = binName;
    else
        fullPath = g_Storage.Config.BinaryPath / binName;

    int fd = open(fullPath.c_str(), O_RDONLY);
    if (fd < 0)
    {
        std::cerr << "ERROR: Library filed to open at: " << fullPath.string() << "\n";
        close(fd);
        return nullptr;
    }

    uint32_t libHash = generate_hash(fullPath.filename().string());

    if (cd->Libraries.find(libHash) != cd->Libraries.end())
    {
        return cd->Libraries.at(libHash).get();
    }

    auto& storage = g_Storage.Active;

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
        traverse_and_collect(cu.root(), *(new std::vector<std::string>), g_Storage, symbols);

    close(fd);

    update_symbol_namespaces(symbols);

    symbols = process_library(ef, symbols, storage);

    void* instance = dlopen(fullPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!instance)
    {
        std::cerr << "DLOPEN ERROR: " << dlerror() << "\n";
        return nullptr;
    }

    std::unordered_map<uint32_t, std::unique_ptr<CapyClass>> collectedClasses;
    std::unordered_map<uint32_t, std::unique_ptr<CapyClass>> collectedStructs;

    for (auto& sym : symbols)
    {
        void* handle = nullptr;
        if (!sym.Signature.empty())
            handle = dlsym(instance, sym.Signature.c_str());
        
        uint32_t callHash = generate_hash(sym.Name);

        // Try internal calls first (highest priority)
        if (g_Storage.Active.InternalCalls.contains(callHash))
        {
            void** handleAddr = reinterpret_cast<void**>(handle);
            *handleAddr = g_Storage.Active.InternalCalls[callHash];
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

        if (isCore)
        {
            g_Storage.Config.CoreDataStructures[classHash] = { sym.Namespace, sym.ClassName };
        }
        else if (g_Storage.Config.CoreDataStructures.contains(classHash))
            continue;

        bool found = false;

        if (sym.IsStruct)
        {
            auto it = collectedStructs.find(classHash);
            if (it == collectedStructs.end()) found = false;
            else
            {
                klass = it->second.get();
                found = true;
            }
        }
        else
        {
            auto it = collectedClasses.find(classHash);
            if (it == collectedClasses.end()) found = false;
            else
            {
                klass = it->second.get();
                found = true;
            }
        }

        if (!found)
        {
            auto newKlass = std::make_unique<CapyClass>();
            newKlass->NameSpace = sym.Namespace;
            newKlass->ClassName = sym.ClassName;
            newKlass->VTable = std::make_unique<CapyVTable>();
            klass = newKlass.get();
            if (sym.IsStruct)
                collectedStructs[classHash] = std::move(newKlass);
            else
                collectedClasses[classHash] = std::move(newKlass);
        }

        if (sym.IsVariable)
        {
            auto field = std::make_unique<CapyField>();
            field->SymHandle = handle;
            field->FieldType = string_to_value_type(sym.ReturnType);
            field->FieldTypeString = sym.ReturnType;
            field->Offset = sym.Offset;
            field->Size = type_size(field->FieldType);
            field->ClassMember = sym.IsVariable && sym.IsClassInstance;
            field->SymbolMetaData = sym;
            std::string fullName;
            if (!field->SymbolMetaData.Namespace.empty() && !field->SymbolMetaData.ClassName.empty())
                fullName = field->SymbolMetaData.Namespace + "::" + field->SymbolMetaData.ClassName;
            else if (!field->SymbolMetaData.Namespace.empty())
                fullName = field->SymbolMetaData.Namespace;
            if (field->SymbolMetaData.ClassName.empty())
                fullName = field->SymbolMetaData.ClassName;
            fullName += "::" + field->SymbolMetaData.Name;
            uint32_t fieldHash = generate_hash(fullName);
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

    image->Classes = std::move(collectedClasses);
    image->Structures = std::move(collectedStructs);

    std::unique_ptr<CapyLibrary> library = std::make_unique<CapyLibrary>(std::move(image));
    library->SymbolInstance = std::move(instance);
    library->IsCore = isCore;
    library->Name = fullPath.filename().string();

    if (isCore)
    {
        auto& binaryPaths = g_Storage.Active.JITStorage.CoreBinaryPaths;
        cd->CoreLibraries.push_back(fullPath.filename().c_str());
        bool found = false;
        for (auto& path : binaryPaths)
        {
            if (path == fullPath)
            {
                found = true;
                break;
            }
        }

        if (!found)
            binaryPaths.push_back(fullPath);
    }

    cd->Libraries[libHash] = std::move(library);

    return cd->Libraries.at(libHash).get();
}

std::vector<std::string> capy_get_core_libraries_from_domain()
{
    auto* cd = g_Storage.Active.Runtime.get();

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

    if (i->Structures.find(classHash) != i->Structures.end())
        return i->Structures.at(classHash).get();

    return nullptr;
}

CapyClass* capy_class_from_name(const std::string& nameSpace, const std::string& className)
{
    std::string fullName;
    if (!nameSpace.empty() && !className.empty())
        fullName += nameSpace + "::" + className;
    else if (!nameSpace.empty())
        fullName += nameSpace;
    else if (!className.empty())
        fullName += className;

    uint32_t classHash = generate_hash(fullName);
    for (auto& [_, libraries] : g_Storage.Active.Runtime->Libraries)
    {
        CapyImage* i = libraries->Image.get();

        if (i->Classes.find(classHash) != i->Classes.end())
            return i->Classes.at(classHash).get();

        if (i->Structures.find(classHash) != i->Structures.end())
            return i->Structures.at(classHash).get();
    }

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

void capy_field_data_get(CapyObject* instance, CapyClass* cc, const std::string& fieldName, void* value)
{
    auto* cd = g_Storage.Active.Runtime.get();

    if (!cd)
    {
        std::cerr << "ERROR: Domain not set!\nBe sure to call capy_jit_init!\n";
        return;
    }

    CapyField* cf = capy_field_from_class(cc, fieldName);

    if (!cf) return;

    if (instance && instance->Memory)
    {
        void* ptr = static_cast<char*>(instance->Memory) + cf->Offset;
        memcpy(value, ptr, type_size(cf->FieldType));
    }
    else
    {
        memcpy(value, cf->DefaultData.raw_ptr(), type_size(cf->FieldType));
    }
}

void capy_field_data_get(CapyObject* instance, CapyField* cf, void* value)
{
    auto* cd = g_Storage.Active.Runtime.get();

    if (!cd)
    {
        std::cerr << "ERROR: Domain not set!\nBe sure to call capy_jit_init!\n";
        return;
    }

    if (!cf) return;

    if (instance && instance->Memory)
    {
        void* ptr = static_cast<char*>(instance->Memory) + cf->Offset;
        memcpy(value, ptr, type_size(cf->FieldType));
    }
    else
    {
        memcpy(value, cf->DefaultData.raw_ptr(), type_size(cf->FieldType));
    }
}

void capy_field_data_set(CapyObject* instance, CapyClass* cc, const std::string& fieldName, void* value)
{
    auto* cd = g_Storage.Active.Runtime.get();

    if (!cd)
    {
        std::cerr << "ERROR: Domain not set!\nBe sure to call capy_jit_init!\n";
        return;
    }

    CapyField* cf = capy_field_from_class(cc, fieldName);
    if (!cf) return;

    if (instance && instance->Memory)
    {
        void* ptr = static_cast<char*>(instance->Memory) + cf->Offset;
        memcpy(ptr, value, type_size(cf->FieldType));
    }
    else
    {
        memcpy(cf->DefaultData.raw_ptr(), value, type_size(cf->FieldType));
    }
}

void capy_field_data_set(CapyObject* instance, CapyField* cf, void* value, uint64_t customSize, uint64_t customOffset)
{
    auto* cd = g_Storage.Active.Runtime.get();

    if (!cd)
    {
        std::cerr << "ERROR: Domain not set!\nBe sure to call capy_jit_init!\n";
        return;
    }


    auto& storage = g_Storage.Active;
    if (!cf) {
        fprintf(stderr, "capy_field_data_set: cf == nullptr\n");
        return;
    }

    // If you have owner class size in metadata, validate bounds:
    size_t size = customSize > 0 ? customSize : type_size(cf->FieldType);

    if (size == 0)
    {
        size = capy_get_class_or_struct_size(cf->SymbolMetaData.Namespace, cf->SymbolMetaData.ReturnType);
    }

    uint64_t offset = cf->Offset + customOffset;



    if (instance && instance->Memory)
    {
        void* ptr = static_cast<char*>(instance->Memory) + offset;
        memcpy(ptr, value, size); // default memcpy
    }
    else
    {
        memcpy(cf->DefaultData.raw_ptr(), value, size);
    }
}


void* capy_function_call_from_method(CapyMethod* method, const std::vector<RuntimeValue>& values)
{
    auto* cd = g_Storage.Active.Runtime.get();

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
    auto* cd = g_Storage.Active.Runtime.get();

    auto& storage = g_Storage.Active;
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

void capy_jit_set_fs_event_callback(FileEventCallback callback)
{
    g_Storage.Active.JITStorage.WatcherStorage.EventCallback = callback;
}

CapyDomain* capy_get_root_domain()
{
    auto* cd = g_Storage.Active.Runtime.get();
    if (!cd)
    {
        std::cerr << "ERROR: Domain not set!\nBe sure to call capy_jit_init!\n";
        return nullptr;
    }

    return cd;
}

constexpr size_t CLASS_ALIGNMENT = 16;

CapyObject* capy_instantiate_object(CapyClass* klass)
{
    if (!klass || klass->ClassSize == 0)
        return nullptr;

    // Compute total class size including base + derived padding
    size_t derivedOwnSize = klass->ClassSize - klass->BaseClassSize; // fields added in derived
    size_t offset = (klass->BaseClassSize + CLASS_ALIGNMENT - 1) & ~(CLASS_ALIGNMENT - 1); // base -> derived padding

    size_t totalSize = (offset + derivedOwnSize + CLASS_ALIGNMENT - 1) & ~(CLASS_ALIGNMENT - 1);

    // Allocate aligned memory
    void* mem = std::aligned_alloc(CLASS_ALIGNMENT, totalSize);
    if (!mem)
        return nullptr;

    std::memset(mem, 0, totalSize);

    auto obj = std::make_unique<CapyObject>(mem, klass);
    CapyObject* raw = obj.get();

    std::string fullName;
    if (!klass->NameSpace.empty() && !klass->ClassName.empty())
        fullName += klass->NameSpace + "::" + klass->ClassName;
    else if (!klass->NameSpace.empty())
        fullName += klass->NameSpace;
    else if (!klass->ClassName.empty())
        fullName += klass->ClassName;

    uint32_t classHash = generate_hash(fullName);

    g_Storage.Active.Runtime->LiveObjects[classHash] = std::move(obj);

    return raw;
}
