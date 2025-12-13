#pragma once

#include <thread>
#include <functional>

// This stores simple file data, i.e. Path and Last Write Time
struct WatchedFile
{
    std::filesystem::path Path;
    std::filesystem::file_time_type WriteTime;
};

// Callback Types
using FileCallback = std::function<void(const std::filesystem::path&)>;

struct DirectoryWatcher
{
    std::filesystem::path Directory;
    std::vector<WatchedFile> Files;
    // This is set up as a vector for when we pause
    // file compilation
    std::vector<WatchedFile> UpdatedFiles;
    int IntervalMs;
    std::atomic<bool> Running;
    std::thread Worker;

    // Callbacks
    FileCallback OnCreate = nullptr;
    FileCallback OnModify = nullptr;
    FileCallback OnDelete = nullptr;
};

// This is a helper function that helps index of a file
int find_file(const DirectoryWatcher& w, const std::string& path);

// This function will poll the given directory for
// new files, updated files, and deleted files
void poll_watcher(DirectoryWatcher& w);

// This function is used to update the worker thread
void worker_thread(DirectoryWatcher* w);

// This function is used to start a new DirectoryWatcher
void start_watcher(DirectoryWatcher& w, const std::filesystem::path& dir, int intervalMs = 200);

// This function is used to stop a DirectoryWatcher, killing the thread
void stop_watcher(DirectoryWatcher& w);
