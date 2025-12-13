#include <cpypch.h>
#include "fswatcher/fswatcher.h"

int find_file(const DirectoryWatcher& w, const std::filesystem::path& path)
{
    for (size_t i = 0; i < w.Files.size(); i++)
        if (w.Files[i].Path == path)
            return (int)i;
    return -1;
}

// Poll function, checks for created/modified/deleted files
void poll_watcher(DirectoryWatcher& w)
{
    // Check for new/modified files
    for (auto& entry : std::filesystem::directory_iterator(w.Directory))
    {
        if (!std::filesystem::is_regular_file(entry)) continue;

        const std::filesystem::path& path = entry.path();
        const auto writeTime = std::filesystem::last_write_time(entry);

        int index = find_file(w, path);
        if (index < 0)
        {
            w.Files.push_back({path, writeTime});
            if (w.OnCreate) w.OnCreate(path);
            if (w.OnCreateCustom) w.OnCreateCustom(path);
        }
        else if (w.Files[index].WriteTime != writeTime)
        {
            w.Files[index].WriteTime = writeTime;
            if (w.OnModify) w.OnModify(path);
            if (w.OnModifyCustom) w.OnModifyCustom(path);
        }
    }

    // Check for deleted files
    for (size_t i = 0; i < w.Files.size(); )
    {
        if (!std::filesystem::exists(w.Files[i].Path))
        {
            std::string deletedFile = w.Files[i].Path;
            w.Files[i] = w.Files.back();
            w.Files.pop_back();
            if (w.OnDelete) w.OnDelete(deletedFile);
            if (w.OnDeleteCustom) w.OnDeleteCustom(deletedFile);
        }
        else
        {
            i++;
        }
    }
}

void watcher_thread(DirectoryWatcher* w)
{
    while (w->Running)
    {
        poll_watcher(*w);
        std::this_thread::sleep_for(std::chrono::milliseconds(w->IntervalMs));
    }
}

void start_watcher(DirectoryWatcher& w, const std::filesystem::path& dir, int intervalMs)
{
    w.Directory = dir;
    w.IntervalMs = intervalMs;
    w.Running = true;

    // Initial scan
    for (auto& entry : std::filesystem::directory_iterator(dir))
    {
        if (!std::filesystem::is_regular_file(entry)) continue;
        w.Files.push_back({entry.path().string(), std::filesystem::last_write_time(entry)});
    }

    // Launch worker thread
    w.Worker = std::thread(watcher_thread, &w);
}

void stop_watcher(DirectoryWatcher& w)
{
    w.Running = false;
    if (w.Worker.joinable())
        w.Worker.join();
}
