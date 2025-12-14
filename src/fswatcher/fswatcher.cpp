#include <cpypch.h>
#include "fswatcher/fswatcher.h"

#include "util/string_util.h"

#include <unordered_set>

// Poll function, checks for created/modified/deleted files
void fswatcher_poll_watcher(DirectoryWatcher& w, DirectoryWatcherStorage& storage)
{
    std::unordered_set<std::filesystem::path> seen;
    
    // Check for new/modified files
    try { for (auto& entry : std::filesystem::directory_iterator(w.Directory))
    {
        if (!entry.is_regular_file()) continue;

        const auto& path = entry.path();
        const auto writeTime = std::filesystem::last_write_time(entry);
        const auto size = entry.file_size();

        seen.insert(path);

        auto it = w.Files.find(path);
        if (it == w.Files.end())
        {
            w.Files[path] = { writeTime, size };

            std::lock_guard lock(storage.EventMutex);
            storage.Events.push_back({path, FileEventType::Create });
            storage.HasPendingEvents = true;
            storage.LastEventTime = std::chrono::steady_clock::now();
        }
        else if (it->second.WriteTime != writeTime || it->second.Size != size)
        {
            w.Files[path] = { writeTime, size };

            std::lock_guard lock(storage.EventMutex);
            storage.Events.push_back({path, FileEventType::Modify });
            storage.HasPendingEvents = true;
            storage.LastEventTime = std::chrono::steady_clock::now();
        }

    }} catch (const std::filesystem::filesystem_error& e) { /* Filesystem error due to race condition, ignore */ }

    // Check for deleted files
    for (auto it = w.Files.begin(); it != w.Files.end(); )
    {
        if (!seen.contains(it->first))
        {
            std::lock_guard lock(storage.EventMutex);
            storage.Events.push_back({it->first, FileEventType::Delete });
            storage.HasPendingEvents = true;
            storage.LastEventTime = std::chrono::steady_clock::now();

            it = w.Files.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void fswatcher_worker_thread(DirectoryWatcherStorage* storage)
{
    while (storage->Running)
    {
        for (auto& [path, watcher] : storage->Watchers)
        {
            if (watcher->Running)
                fswatcher_poll_watcher(*watcher, *storage);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }}

void fswatcher_start_storage(DirectoryWatcherStorage& storage)
{
    storage.Running = true;
    storage.Worker = std::thread(fswatcher_worker_thread, &storage);
}

void fswatcher_stop_storage(DirectoryWatcherStorage& storage)
{
    storage.Running = false;
    for (auto& [path, watcher] : storage.Watchers)
        watcher->Running = false;

    if (storage.Worker.joinable())
        storage.Worker.join();
}

void fswatcher_add_watcher(DirectoryWatcherStorage& storage, const std::filesystem::path& dir, bool recursive, int intervalMS)
{
    auto watcher = std::make_unique<DirectoryWatcher>();
    watcher->Directory = dir;
    watcher->IntervalMs = intervalMS;
    watcher->Running = true;

    storage.Watchers[dir] = std::move(watcher);

    if (recursive)
    {
        for (auto& entry : std::filesystem::recursive_directory_iterator(dir))
        {
            if (!entry.is_directory())
                continue;

            if (storage.IgnoreHiddenPaths)
            {
                std::string name = entry.path().filename().string();
                // Check if the name starts with a dot and is not just "." or ".."
                if (name.length() > 0 && name[0] == '.' && name != "." && name != "..") {
                    continue;
                }
            }

            auto watcher = std::make_unique<DirectoryWatcher>();
            watcher->Directory = entry.path();
            watcher->IntervalMs = intervalMS;
            watcher->Running = true;

            storage.Watchers[entry.path()] = std::move(watcher);
        }
    }
}

void fswatcher_dispatch_events(DirectoryWatcherStorage& storage)
{
    std::vector<FileEvent> events;
    {
        std::lock_guard lock(storage.EventMutex);
        events.swap(storage.Events);
        storage.HasPendingEvents = false;
    }

    for (auto& e : events)
    {
        if (storage.EventCallback)
        {
            if (!strs_n_equal(e.Path.extension().string(), { ".c", ".cpp", ".h", ".hpp" }))
                continue;


            if (e.Path.extension().string().find("~") != std::string::npos)
                continue;


            std::filesystem::path retrievedPath = storage.EventCallback(e.Type, e.Path);

            bool found = false;

            for (auto& path : storage.UpdatedFiles)
            {
                std::filesystem::path extensionLessPath = path; extensionLessPath.replace_extension("");
                std::filesystem::path extensionLessRetrievedPath = retrievedPath; extensionLessRetrievedPath.replace_extension("");

                if (extensionLessRetrievedPath == extensionLessPath)
                    found = true;
            }

            if (!found)
                storage.UpdatedFiles.push_back(retrievedPath);
        }
    }
}

bool fswatcher_should_dispatch_events(DirectoryWatcherStorage& storage, std::chrono::milliseconds debounceTime)
{
    if (!storage.HasPendingEvents) return false;

    auto now = std::chrono::steady_clock::now();
    return (now - storage.LastEventTime) >= debounceTime;
}

void fswatcher_update_file_events(DirectoryWatcherStorage& storage)
{
    if (!fswatcher_should_dispatch_events(storage))
        return;

    fswatcher_dispatch_events(storage);
}
