#include "cpypch.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <ffi.h>

#include "capybara/capybara.h"
#include "capybara/runtime.h"

#include "fswatcher/fswatcher.h"

#include "util/libelf_util.h"
#include "util/ffi_util.h"
#include "util/string_util.h"
#include "util/fswatcher_utils.h"

#include <signal.h>

RuntimeStorage g_Storage;


template<typename T> 
static void merge_defaults(std::vector<T>& dst, const std::vector<T>& defaults)
{ 
    if (dst.empty()) dst = defaults; 
    else dst.insert(dst.end(), defaults.begin(), defaults.end());
}

inline std::vector<CapyString> make_capy_vector(std::initializer_list<const char*> strings)
{
    std::vector<CapyString> v;
    v.reserve(strings.size());
    for (const char* s : strings)
        v.push_back(capy_string_literal(s)); return v;
}

#define CAPY_STRING_VECTOR(name, ...) \
    static std::vector<CapyString> name = make_capy_vector({ __VA_ARGS__ })

CAPY_STRING_VECTOR(s_IGNORED_NAMESPACES, "std", "__gnu", "<anon>", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "_IO_", "_G_");
CAPY_STRING_VECTOR(s_IGNORED_CLASSNAMES, "std", "__gnu", "<anon>", "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "_IO_", "_G_", "__pthread", "timespec", "lconv", "_M_", "tm", "typedef __va_");
CAPY_STRING_VECTOR(s_IGNORED_NAMES, "<anon>", "gp_offset", "fp_offset", "overflow_arg_area", "reg_save_area", "tm_", "__", "_IO", "_flags", "quot", "_markers", "_chain", "_short", "_old_offset", "_cur_column", "_vtable_offset", "_shortbuf", "_lock", "_offset", "_codecvt", "_wide_data", "_freeres", "_prevchain", "_mode", "_unused", "_total_written", "decimal_point", "thousands_sep", "goruping", "int_curr_symbol", "currency_symbol", "mon_decimal_point", "mon_thousands_sep", "mon_grouping", "positive_sign", "negative_sign", "int_frac_digits", "frac_digits", "p_cs_precedes", "p_sep_by_space", "n_cs_", "n_sep_", "p_sign_", "n_sign_", "int_p_", "int_n_", "rem", "_fileno", "grouping");

static inline CapyString join_names(const std::vector<CapyString>& scope_stack)
{
    auto* domain = g_Storage.Active.Runtime.get();
    if (scope_stack.empty()) return capy_string_literal("");
    std::vector<CapyString> goodStack;
    for (auto& scope : scope_stack)
    {
        if (!scope.empty())
            goodStack.push_back(scope);
    }
    std::string tmp;
    for (size_t i = 0; i < goodStack.size(); ++i)
    {
        if (i > 0)
            tmp += "::";
        tmp += goodStack[i].Data; // copy from arena/literal string 
    }
    return capy_string_arena(domain->Arena, tmp.c_str());
}

static uint64_t make_symbol_hash(std::initializer_list<CapyString> parts)
{
    CapyString s = join_names(parts);
    return generate_hash(s.c_str());
}

static std::string jit_get_compile_command(const std::filesystem::path& filePath)
{
    auto& jit = g_Storage.Active.JITStorage;

    
    std::filesystem::path rootDir = std::filesystem::current_path();

    std::filesystem::path compilePath = rootDir / g_Storage.Config.BinaryPath / filePath.filename();
    compilePath.replace_extension(".o");

    std::filesystem::path sourceFile = rootDir / filePath;
    sourceFile.replace_extension(".cpp");

    std::stringstream ss;
    ss << "gcc -c " << sourceFile << " -o " << compilePath.generic_string() << " \\\n";
    if (!jit.CorePath.empty())
    {
        ss << "-I" << jit.CorePath.string() << " -gdwarf-4 -fPIC \\\n";
    }


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

CapyDomain* capy_init()
{
    auto& cfg = g_Storage.Config;
    merge_defaults(cfg.IgnoredNamespaces, s_IGNORED_NAMESPACES);
    merge_defaults(cfg.IgnoredClassNames, s_IGNORED_CLASSNAMES);
    merge_defaults(cfg.IgnoredNames, s_IGNORED_NAMES);

    auto& jit = g_Storage.Active.JITStorage;

    // Create the Domain
    CapyDomain* cd = new CapyDomain;

    g_Storage.Active.Runtime.reset(cd);

    jit.JitRunning = true;
    jit.JitThread = std::thread(jit_worker);

    fswatcher_start_storage(jit.WatcherStorage);

    return cd;
}

void capy_shutdown()
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

bool capy_poll()
{
    std::filesystem::path rootDir = std::filesystem::current_path();
    auto& jit = g_Storage.Active.JITStorage;
    if (jit.JitCompilationNeeded.exchange(false))
    {

        std::vector<std::string> commands;
        {
            std::lock_guard<std::mutex> lock(jit.JitMutex);
            commands.swap(jit.CompilationCommands);
        }

        for (auto& command : commands)
        {
            system(command.c_str());
        }

        std::vector<std::filesystem::path> files_being_compiled;
        {
            std::lock_guard<std::mutex> lock(jit.JitMutex);
            files_being_compiled.swap(jit.FilesToCompile); // move pending files
        }

        for (auto& path : files_being_compiled)
        {
            std::filesystem::path objPath = rootDir / g_Storage.Config.BinaryPath / path.filename();
            objPath.replace_extension(".o");
            while (!std::filesystem::exists(objPath))
                std::this_thread::sleep_for(std::chrono::milliseconds(10));

            uint64_t fileHash = generate_hash(objPath.c_str());

            jit.TrackedObjectFiles[fileHash] = objPath;
        }

        std::unordered_map<uint64_t, std::filesystem::path> trackedFiles;
        {
            std::lock_guard<std::mutex> lock(jit.JitMutex);
            trackedFiles.swap(jit.TrackedObjectFiles);
        }

        {
            std::stringstream ss;
            ss << "gcc -shared -fPIC -gdwarf-4 -lstdc++ ";

            for (auto& [_, path] : trackedFiles)
            {
                ss << path.generic_string() << " ";
            }

            ss << "-o " << g_Storage.Config.BinaryPath.generic_string() << "/CapyBinary.so \\\n";

            std::string coreLibLinks;
            for (auto& lib : g_Storage.Active.Runtime->CoreLibraries)
            {
                std::string libFix;
                std::string libCheck;
                if (strs_n_equal(lib.c_str(), {"lib"}))
                    libCheck = lib.c_str() + 3;
                else 
                    libFix = lib.c_str();

                libFix = libCheck.substr(0, libCheck.length() - 3);

                coreLibLinks.append("-l" + libFix + " ");
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
            ss << coreLibLinks << "\n";

            std::cout << ss.str() << "\n";

            system(ss.str().c_str());
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

void capy_set_core_bin_include_path(const std::filesystem::path &includePath)
{
    g_Storage.Active.JITStorage.CorePath = includePath;
}

void capy_set_source_path(const std::filesystem::path& sourcePath, FileEventCallback callback, bool recursive)
{
    auto& jit = g_Storage.Active.JITStorage;
    fswatcher_add_watcher(jit.WatcherStorage, sourcePath, recursive);
    jit.WatcherStorage.EventCallback = callback;

    std::filesystem::path compilePath = sourcePath / ".capy";
    if (!std::filesystem::exists(compilePath))
        std::filesystem::create_directories(compilePath);

    g_Storage.Config.BinaryPath = compilePath;
}

void capy_set_ignored_namespace(const std::vector<CapyString>& ignoredNamespace)
{
    for (auto& nameSpace : ignoredNamespace)
        g_Storage.Config.IgnoredNamespaces.push_back(nameSpace);
}

void capy_set_ignored_classname(const std::vector<CapyString>& ignoredClassName)
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
    std::string adjustedClassName = klass->ClassName.empty() ? "<Functions Only>" : klass->ClassName.c_str();
    str.append(adjustedClassType + adjustedClassName + " (Total Size: " + std::to_string(klass->ClassSize) + ")");
    if (klass->BaseClassSize > 0)
    {
        str.append("\n    Base Class(es): ");
        bool beg = true;
        for (auto& [_, base] : klass->BaseClasses)
        {
            if (!beg) str.append(", ");
            str.append(base.ClassName.c_str());
            beg = false;
        }
    }
    str.append("\n");

    std::unordered_map<uint64_t, CapyTableMetaData> methodTable = capy_table_info_get(klass->BaseImage, CapyTableType::MethodDef)->Symbols[klass->Hash];
    std::unordered_map<uint64_t, CapyTableMetaData> fieldTable = capy_table_info_get(klass->BaseImage, CapyTableType::FieldDef)->Symbols[klass->Hash];

    for (auto& [hash, sym] : methodTable)
    {
        std::string adjustedSymName;
        if (!sym.Namespace.empty())
            adjustedSymName.append(sym.Namespace.c_str()).append("::");
        if (!sym.ClassName.empty())
            adjustedSymName.append(sym.ClassName.c_str()).append("::");

        adjustedSymName.append(sym.Name.c_str());
        str.append("    (METHOD) ").append(sym.ReturnType.c_str()).append(" " + adjustedSymName + "\n");
    }
    for (auto& [hash, sym] : fieldTable)
    {
        std::string adjustedSymName;
        if (!sym.Namespace.empty())
            adjustedSymName.append(sym.Namespace.c_str()).append("::");
        if (!sym.ClassName.empty())
            adjustedSymName.append(sym.ClassName.c_str()).append( "::");

        adjustedSymName.append(sym.Name.c_str());
        str.append("    (FIELD)  ").append(sym.ReturnType.c_str()).append(" " + adjustedSymName + "\n");
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
        str.append("Library: ").append(lib->Name.c_str());
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

void capy_resolve_class_layout(CapyClass* klass)
{
    if (klass->Resolved)
        return;

    size_t baseSize = 0;

    for (auto& base : g_Storage.Config.ClassMap[klass->Hash])
    {
        CapyClass* baseClass = capy_class_from_name(base.NameSpace.c_str(), base.ClassName.c_str());
        if (!baseClass)
            continue;


        uint64_t classHash = make_symbol_hash({base.NameSpace, base.ClassName});

        if (!klass->BaseClasses.contains(classHash))
            klass->BaseClasses[classHash] = base;

        capy_resolve_class_layout(baseClass);
        baseSize += baseClass->ClassSize;

        for (auto& [fieldHash, baseField] : baseClass->VTable->Fields)
        {
            // Copy base field into derived
            auto copiedField = std::make_unique<CapyField>(*baseField);
            klass->SubFields[fieldHash] = {
                copiedField->Size,
                copiedField->Offset + (baseSize - baseClass->ClassSize)
            };
        }

        for (auto& [baseHash, baseKlass] : baseClass->BaseClasses)
            klass->BaseClasses[baseHash] = baseKlass;
    }

    klass->BaseClassSize = baseSize;

    size_t offset = baseSize;

    // sort by declared order
    std::vector<CapyField*> fields;
    for (auto& [_, f] : klass->VTable->Fields)
        fields.push_back(f.get());

    std::sort(fields.begin(), fields.end(),
        [](CapyField* a, CapyField* b) { return a->Offset < b->Offset; });

    for (auto* field : fields)
    {
        if (field->Size == 0)
        {
            CapyClass* type = capy_class_from_name(klass->NameSpace.c_str(), field->FieldTypeString.c_str());
            if (type)
            {
                capy_resolve_class_layout(type);
                field->Size = type->ClassSize;

                for (auto& [fieldHash, fld] : type->VTable->Fields)
                {
                    field->SubFields[fieldHash] = { fld->Size, fld->Offset };
                }
            }
        }

        field->Offset = offset;
        offset += field->Size;

    }

    klass->ClassSize = offset;
    klass->Resolved = true;
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
        for (auto& [_, klass] : lib->Image->Classes)
            capy_resolve_class_layout(klass.get());
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

    uint64_t libHash = generate_hash(fullPath.filename().string());

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
    std::vector<CapySymbolMetaData> symbols;

    for (auto &cu : dw.compilation_units())
        traverse_and_collect(cu.root(), *(new std::vector<CapyString>), g_Storage, symbols);

    close(fd);

    symbols = process_library(ef, symbols, storage);

    void* instance = dlopen(fullPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!instance)
    {
        std::cerr << "DLOPEN ERROR: " << dlerror() << "\n";
        return nullptr;
    }

    for (auto& sym : symbols)
    {
        void* handle = nullptr;
        if (!sym.Signature.empty())
            handle = dlsym(instance, sym.Signature.c_str());
        
        uint64_t callHash = generate_hash(sym.Name.c_str());

        // Try internal calls first (highest priority)
        if (g_Storage.Active.InternalCalls.contains(callHash))
        {
            void** handleAddr = reinterpret_cast<void**>(handle);
            *handleAddr = g_Storage.Active.InternalCalls[callHash];
        }

        if (!handle && (!sym.IsVariable && !sym.IsClassInstance))
            continue;

        CapyString nameSpace = capy_string_intern(sym.Namespace);
        CapyString className = capy_string_intern(sym.ClassName);
        CapyString name = capy_string_intern(sym.Name);

        uint64_t classHash = make_symbol_hash({capy_string_intern(sym.Namespace), capy_string_intern(sym.ClassName)});

        // Ensure class exists or create new
        CapyClass* klass = nullptr;

        if (isCore)
        {
            g_Storage.Config.CoreDataStructures[classHash] = { nameSpace, className };
        }
        else if (g_Storage.Config.CoreDataStructures.contains(classHash))
            continue;

        bool found = false;

        if (sym.IsStruct)
        {
            auto it = image->Structures.find(classHash);
            if (it == image->Structures.end()) found = false;
            else
            {
                klass = it->second.get();
                found = true;
            }
        }
        else
        {
            auto it = image->Classes.find(classHash);
            if (it == image->Classes.end()) found = false;
            else
            {
                klass = it->second.get();
                found = true;
            }
        }

        if (!found)
        {
            auto newKlass = std::make_unique<CapyClass>();
            newKlass->NameSpace = nameSpace;
            newKlass->ClassName = className;
            newKlass->VTable = std::make_unique<CapyVTable>();
            newKlass->Hash = classHash;
            newKlass->BaseImage = image.get();
            klass = newKlass.get();
            
            if (sym.IsStruct)
                image->Structures[classHash] = std::move(newKlass);
            else
                image->Classes[classHash] = std::move(newKlass);

            CapyTableInfo* table = capy_table_info_get(image.get(), CapyTableType::TypeDef);

            CapyTableMetaData tableMetaData =
            {
                sym.Namespace, sym.ClassName, CapyString(), CapyString(), false, false, sym.IsStruct
            };

            table->Symbols[classHash][0] = tableMetaData;

        }

        if (sym.IsVariable)
        {
            auto field = std::make_unique<CapyField>();
            field->Name = name;
            field->SymHandle = handle;
            field->FieldType = string_to_value_type(sym.ReturnType.c_str());
            field->FieldTypeString = capy_string_intern(sym.ReturnType);
            field->Offset = sym.Offset;
            field->Size = type_size(field->FieldType);
            field->ClassMember = sym.IsVariable && sym.IsClassInstance;
            
            uint64_t fieldHash = make_symbol_hash({nameSpace, className, name});

            CapyTableInfo* table = capy_table_info_get(image.get(), CapyTableType::FieldDef);
            CapyTableMetaData metaData = { sym.Namespace, sym.ClassName, sym.Name, sym.ReturnType, sym.IsVariable, sym.IsClassInstance, sym.IsStruct };

            table->Symbols[classHash][fieldHash] = metaData;

            klass->VTable->Fields[fieldHash] = std::move(field);
        }
        else
        {
            auto method = std::make_unique<CapyMethod>();
            method->Name = name;
            method->SymHandle = handle;
            method->ReturnType = string_to_value_type(sym.ReturnType.c_str());

            if (sym.IsClassInstance)
            {
                method->ClassMember = true;
                method->Parameters.push_back(ValueType::POINTER);
            }
            for (auto& param : sym.ParameterTypes)
            {
                ValueType type = string_to_value_type(param.c_str());
                if (type != ValueType::VOID)
                    method->Parameters.push_back(type);
            }

            uint64_t methodHash = make_symbol_hash({nameSpace, className, name});

            CapyTableInfo* table = capy_table_info_get(image.get(), CapyTableType::MethodDef);
            CapyTableMetaData metaData = { sym.Namespace, sym.ClassName, sym.Name, sym.ReturnType, sym.IsVariable, sym.IsClassInstance, sym.IsStruct };

            table->Symbols[classHash][methodHash] = metaData;

            klass->VTable->Methods[methodHash] = std::move(method);
        }
    }

    std::unique_ptr<CapyLibrary> library = std::make_unique<CapyLibrary>(std::move(image));
    library->SymbolInstance = std::move(instance);
    library->IsCore = isCore;

    CapyString libName = capy_string_intern(fullPath.filename().c_str());

    library->Name = libName;

    if (isCore)
    {
        auto& binaryPaths = g_Storage.Active.JITStorage.CoreBinaryPaths;
        cd->CoreLibraries.push_back(libName);
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

std::vector<CapyString> capy_get_core_libraries_from_domain()
{
    auto* cd = g_Storage.Active.Runtime.get();

    if (!cd)
    {
        std::cerr << "ERROR: Domain not set!\nBe sure to call capy_jit_init!\n";
        return {};
    }

    return cd->CoreLibraries;
}

CapyImage* capy_library_get_image(CapyLibrary* l)
{
    if (!l)
        return nullptr;

    return l->Image.get();
}

CapyTableInfo* capy_table_info_get(CapyImage* ci, CapyTableType type)
{
    if (!ci) return nullptr;
    uint32_t index = static_cast<uint32_t>(type);
    if (index > ci->Tables.size()) return nullptr;
    if (!ci->Tables[index])
        ci->Tables[index] = std::make_unique<CapyTableInfo>();

    return ci->Tables[index].get();
}

CapyClass* capy_class_from_name(CapyImage* i, const std::string& nameSpace, const std::string& className)
{
    std::string fullName;
    if (!nameSpace.empty())
        fullName += nameSpace + "::";
    if (!className.empty())
        fullName += className;

    uint64_t classHash = generate_hash(fullName);
    if (i->Classes.find(classHash) != i->Classes.end())
        return i->Classes.at(classHash).get();

    if (i->Structures.find(classHash) != i->Structures.end())
        return i->Structures.at(classHash).get();

    return nullptr;
}

CapyClass* capy_class_from_name(const std::string& nameSpace, const std::string& className)
{
    std::string fullName;
    if (!nameSpace.empty())
        fullName += nameSpace + "::";
    if (!className.empty())
        fullName += className;

    uint64_t classHash = generate_hash(fullName);
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
    uint64_t funcHash = make_symbol_hash({ c->NameSpace, c->ClassName, capy_string_intern(functionName.c_str()) });
    return c->VTable->Methods[funcHash].get();
}

CapyField* capy_field_from_class(CapyClass* c, const std::string& fieldName)
{
    uint64_t fieldHash = make_symbol_hash({ c->NameSpace, c->ClassName, capy_string_intern(fieldName.c_str()) });
    return c->VTable->Fields[fieldHash].get();
}

std::vector<CapyField*> capy_fields_from_class(CapyClass* cc)
{
    std::vector<CapyField*> tmp;

    for (auto& [_, fld] : cc->VTable->Fields)
    {
        tmp.push_back(fld.get());
    }

    return tmp;
}

struct FieldResolveResult
{
    size_t Offset;
    size_t Size;
};

static bool resolve_field(CapyClass* cc, const std::string& fieldName, FieldResolveResult& out)
{
    std::string initailFieldName = fieldName;
    CapyString subFieldName;

    size_t dotOperator = fieldName.find(".");
    if (dotOperator != std::string::npos)
    {
        initailFieldName = fieldName.substr(0, dotOperator);
        subFieldName = capy_string_intern(fieldName.substr(dotOperator + 1).c_str());
    }

    CapyString adjustedFieldName = capy_string_intern(initailFieldName.c_str());
    uint64_t fieldHash = make_symbol_hash({cc->NameSpace, cc->ClassName, adjustedFieldName});
    CapyField* cf = cc->VTable->Fields[fieldHash].get();
    if (!cf && subFieldName.empty()) return false;

    size_t sizeOverride = 0;
    size_t offsetOverride = 0;
    bool obtainedField = false;

    if (cf && subFieldName.empty())
    {
        sizeOverride = cf->Size;
        offsetOverride = cf->Offset;
        obtainedField = true;
    }
    else if (!subFieldName.empty() && cf)
    {
        CapyString nameSpace = capy_string_intern(cc->NameSpace);
        CapyString returnType = capy_string_intern(cf->FieldTypeString);
        uint64_t fieldHash = make_symbol_hash({ nameSpace, returnType, subFieldName});
        sizeOverride = cf->SubFields[fieldHash].Size;
        offsetOverride = cf->SubFields[fieldHash].Offset + cf->Offset;
        obtainedField = true;
    }
    else
    {
        for (auto& [_, baseClass] : cc->BaseClasses)
        {
            uint64_t fieldHash = make_symbol_hash({ baseClass.NameSpace, baseClass.ClassName, subFieldName});
            if (!cc->SubFields.contains(fieldHash)) continue;
            sizeOverride = cc->SubFields[fieldHash].Size;
            offsetOverride = cc->SubFields[fieldHash].Offset;
            obtainedField = true;
            break;
        }
    }

    if (!obtainedField) return false;

    out.Offset = offsetOverride;
    out.Size = sizeOverride;

    return true;
}

void capy_field_data_get(CapyObject* instance, CapyClass* cc, const std::string& fieldName, void* value)
{
    auto* cd = g_Storage.Active.Runtime.get();

    if (!cd)
    {
        std::cerr << "ERROR: Domain not set!\nBe sure to call capy_jit_init!\n";
        return;
    }

    FieldResolveResult res;
    if (!resolve_field(cc, fieldName, res)) return;

    if (instance && instance->Memory)
    {
        void* ptr = static_cast<char*>(instance->Memory) + res.Offset;
        memcpy(value, ptr, res.Size);
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

    FieldResolveResult res;
    if (!resolve_field(cc, fieldName, res)) return;

    if (instance && instance->Memory)
    {
        void* ptr = static_cast<char*>(instance->Memory) + res.Offset;
        memcpy(ptr, value, res.Size);
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
    uint64_t callHash = generate_hash(name);
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
                uint64_t nameHash = generate_hash(name);
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

    CapyString fullName = join_names({klass->NameSpace, klass->ClassName});

    uint64_t classHash = generate_hash(fullName.c_str());

    g_Storage.Active.Runtime->LiveObjects[classHash] = std::move(obj);

    return raw;
}
