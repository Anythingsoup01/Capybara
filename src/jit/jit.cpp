#include <cpypch.h>
#include "jit/jit.h"

#include "capybara/capybara.h"
#include "util/fswatcher_utils.h"

static std::string jit_get_compile_command(const std::filesystem::path& filePath)
{
    auto& jit = s_Storage.JITStorage;
    std::string coreLibLinks;
    for (auto& lib : jit.JitDomain->CoreLibraries)
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

    std::filesystem::path compilePath = rootDir / jit.BinaryPath / filePath.filename();
    compilePath.replace_extension(".so");

    std::filesystem::path sourceFile = rootDir / filePath;
    sourceFile.replace_extension(".cpp");

    std::stringstream ss;
    ss << "gcc " << sourceFile << " -o " << compilePath << " \\\n";
    if (jit.CorePaths.size() > 0)
    {
        for (auto& path : jit.CorePaths)
        {
            std::filesystem::path truePath = rootDir / path;
            ss << "-I" << truePath.string() << " ";
        }
        ss << "\\\n";
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
    auto& jit = s_Storage.JITStorage;
    while (jit.JitRunning)
    {
        bool hasWork = false;

        {
            std::lock_guard<std::mutex> lock(jit.PendingFileMutex);

            if (!jit.PendingFiles.empty())
            {
                jit.FilesToCompile.swap(jit.PendingFiles);
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

CapyDomain* capy_jit_init(const std::string &domainName)
{
    auto& jit = s_Storage.JITStorage;
    // Initialize Capy
    capy_init();

    // Create the Domain
    CapyDomain* cd = capy_init_domain(domainName);

    jit.JitRunning = true;
    jit.JitThread = std::thread(jit_worker);

    jit.JitDomainHash = generate_hash(domainName);
    jit.JitDomain = cd;
    jit.DomainName = domainName;

    return cd;
}

bool capy_jit_poll()
{
    std::filesystem::path rootDir = std::filesystem::current_path();
    auto& jit = s_Storage.JITStorage;
    if (jit.JitCompilationNeeded.exchange(false))
    {
        std::vector<std::filesystem::path> files_being_compiled;

        {
            std::lock_guard<std::mutex> lock(jit.PendingFileMutex);
            files_being_compiled.swap(jit.FilesToCompile); // move pending files
        }

        std::vector<std::string> commands;

        {
            std::lock_guard<std::mutex> lock(jit.PendingFileMutex);
            commands.swap(jit.CompilationCommands);
        }

        for (auto& command : commands)
        {
            std::cout << command << "\n";
            system(command.c_str());
        }

        for (auto& path : files_being_compiled)
        {
            std::filesystem::path soPath = rootDir / jit.BinaryPath / path.filename();
            soPath.replace_extension(".so");
            while (!std::filesystem::exists(soPath))
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        capy_unload_domain(jit.JitDomainHash);

        jit.JitDomain = capy_init_domain(jit.DomainName);

        for (auto& lib : jit.CoreBinaryPaths)
        {
            capy_domain_library_open(jit.JitDomain, lib, true);
        }

        capy_reload_libraries_into_domain(jit.JitDomain);
        return true;
    }
    return false;
}

void capy_jit_set_source_path(const std::filesystem::path& sourcePath, bool recursive)
{
    auto& jit = s_Storage.JITStorage;
    std::vector<std::unique_ptr<DirectoryWatcher>> watchers;

    auto w = std::make_unique<DirectoryWatcher>();
    w->OnCreate = fswatcher_on_create;
    w->OnCreateCustom = jit.FileWatcherOnCreate;
    w->OnModify = fswatcher_on_modified;
    w->OnModifyCustom = jit.FileWatcherOnModify;
    w->OnDelete = fswatcher_on_deleted;
    w->OnDeleteCustom = jit.FileWatcherOnDelete;
    start_watcher(*w, sourcePath);
    watchers.push_back(std::move(w));

    if (recursive)
    {
        for (auto& entry : std::filesystem::recursive_directory_iterator(sourcePath))
        {
            if (entry.is_directory())
            {
                auto w = std::make_unique<DirectoryWatcher>();
                w->OnCreate = fswatcher_on_create;
                w->OnCreateCustom = jit.FileWatcherOnCreate;
                w->OnModify = fswatcher_on_modified;
                w->OnModifyCustom = jit.FileWatcherOnModify;
                w->OnDelete = fswatcher_on_deleted;
                w->OnDeleteCustom = jit.FileWatcherOnDelete;
                start_watcher(*w, entry.path().string());
                watchers.push_back(std::move(w));
            }
        }
    }


    jit.FileWatchers = std::move(watchers);
}

void capy_jit_set_binary_path(const std::filesystem::path& binaryPath)
{
    s_Storage.JITStorage.BinaryPath = binaryPath;
    capy_set_libraries_path(binaryPath);
}

void capy_jit_add_core_library(const std::filesystem::path& libPath, const std::filesystem::path& libBinaryPath)
{
    auto& jit = s_Storage.JITStorage;

    capy_domain_library_open(jit.JitDomain, libBinaryPath.string(), true);

    {
        bool found = false;

        for (auto& path : jit.CorePaths)
        {
            if (path == libPath)
            {
                found = true;
                break;
            }
        }

        if (!found)
            jit.CorePaths.push_back(libPath);
    }
    {
        bool found = false;

        for (auto& path : jit.CoreBinaryPaths)
        {
            if (path == libBinaryPath)
            {
                found = true;
                break;
            }
        }

        if (!found)
            jit.CoreBinaryPaths.push_back(libBinaryPath);
    }

}

void capy_jit_set_fw_on_create(FileCallback callback)
{
    for (auto& fw : s_Storage.JITStorage.FileWatchers)
    {
        fw->OnCreateCustom = callback;
    }

    s_Storage.JITStorage.FileWatcherOnCreate = callback;
}

void capy_jit_set_fw_on_modify(FileCallback callback)
{
    for (auto& fw : s_Storage.JITStorage.FileWatchers)
    {
        fw->OnModifyCustom = callback;
    }

    s_Storage.JITStorage.FileWatcherOnModify = callback;
}

void capy_jit_set_fw_on_delete(FileCallback callback)
{
    for (auto& fw : s_Storage.JITStorage.FileWatchers)
    {
        fw->OnDeleteCustom = callback;
    }

    s_Storage.JITStorage.FileWatcherOnDelete = callback;
}

CapyDomain* capy_jit_get_domain()
{
    return s_Storage.JITStorage.JitDomain;
}
