#pragma once

#include <queue>
#include <mutex>
#include <thread>
#include <atomic>


namespace Capybara {

struct FileChangeEvent {
  std::filesystem::path Path;
  enum Type { Modified = 0, Created, Deleted } type;
};

class FileWatcher {
public:
  FileWatcher() = default;
  ~FileWatcher();

  void Init(const std::filesystem::path &sourceDirectory);

  std::vector<FileChangeEvent> Poll();
private:
  void AddWatchDir(const std::filesystem::path &dir);
  void AddRecursiveWatches(const std::filesystem::path &dir);

  void Run();
private:
  std::filesystem::path m_RootPath;
  int m_INotifyFD = -1;
  std::atomic<bool> m_Running;
  std::thread m_Worker;

  std::mutex m_QueueMutex;
  std::queue<FileChangeEvent> m_PendingEvents;

  std::unordered_map<int, std::filesystem::path> m_WatchDescriptors;

  std::mutex m_WDMutex;
};

}
