#pragma once

#include <thread>
#include <functional>

// This stores simple file data, i.e. Path and Last Write Time
struct WatchedFile
{
    std::filesystem::file_time_type WriteTime;
    uintmax_t Size;
};

enum class FileEventType
{
    None = 0,
    Create,
    Modify,
    Delete,
};

struct DirectoryWatcherStorage;
struct DirectoryWatcher;

struct FileEvent
{
    std::filesystem::path Path;
    FileEventType Type;
};

using FileMap = std::unordered_map<std::filesystem::path, WatchedFile>;

// Callback Types
using FileEventCallback = std::function<std::filesystem::path(FileEventType type, const std::filesystem::path&)>;


struct DirectoryWatcher
{
    std::filesystem::path Directory;
    FileMap Files;

    int IntervalMs = 200;

    std::atomic<bool> Running{false};
};

struct DirectoryWatcherStorage
{
    std::unordered_map<std::filesystem::path, std::unique_ptr<DirectoryWatcher>> Watchers;

    std::vector<FileEvent> Events;

    std::vector<std::filesystem::path> UpdatedFiles;

    std::mutex EventMutex;

    std::atomic<bool> Running{false};
    std::thread Worker;

    FileEventCallback EventCallback = nullptr;
    
    std::chrono::steady_clock::time_point LastEventTime;
    bool HasPendingEvents = false;
};

void fswatcher_poll_watcher(DirectoryWatcher& w, DirectoryWatcherStorage& storage);

void fswatcher_worker_thread(DirectoryWatcherStorage* storage);

void fswatcher_start_storage(DirectoryWatcherStorage& storage);

void fswatcher_stop_storage(DirectoryWatcherStorage& storage);

void fswatcher_add_watcher(DirectoryWatcherStorage& storage, const std::filesystem::path& dir, bool recursive = false, int intervalMS = 200);

bool fswatcher_should_dispatch_events(DirectoryWatcherStorage& storage, std::chrono::milliseconds debounceTime = std::chrono::milliseconds(150));

void fswatcher_update_file_events(DirectoryWatcherStorage& storage);

