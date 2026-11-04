#include "FileWatcher.h"

#include <limits.h>
#include <sys/inotify.h>
#include <unistd.h>

namespace Capybara {

FileWatcher::~FileWatcher() {
  m_Running = false;
  if (m_Worker.joinable()) {
    m_Worker.join();
  }

  if (m_INotifyFD >= 0) {
    close(m_INotifyFD);
  }
}

void FileWatcher::Init(const std::filesystem::path &watchPath) {
  m_RootPath = watchPath;
  m_Running = true;
  m_INotifyFD = inotify_init1(IN_NONBLOCK);
  if (m_INotifyFD < 0) {
    throw std::runtime_error("JIT Error: inotify_init1 failed!");
  }

  AddRecursiveWatches(watchPath);

  m_Worker = std::thread([this]() { Run(); });
}

std::vector<FileChangeEvent> FileWatcher::Poll() {
  std::lock_guard<std::mutex> lock(m_QueueMutex);
  std::vector<FileChangeEvent> drained;
  drained.reserve(m_PendingEvents.size());
  while (!m_PendingEvents.empty()) {
    drained.push_back(std::move(m_PendingEvents.front()));
    m_PendingEvents.pop();
  }
  return drained;
}

void FileWatcher::AddWatchDir(const std::filesystem::path &dir_path) {
  int wd = inotify_add_watch(m_INotifyFD, dir_path.c_str(),
                             IN_MODIFY | IN_CREATE | IN_DELETE | IN_MOVED_FROM |
                                 IN_MOVED_TO);
  if (wd >= 0) {
    std::lock_guard<std::mutex> lock(m_WDMutex);
    m_WatchDescriptors[wd] = dir_path;
  }
}

void FileWatcher::AddRecursiveWatches(const std::filesystem::path &base_path) {
  AddWatchDir(base_path);
  for (auto const &entry :
       std::filesystem::recursive_directory_iterator(base_path)) {
    if (std::filesystem::is_directory(entry.path())) {
      AddWatchDir(entry.path());
    }
  }
}

void FileWatcher::Run() {
  constexpr size_t buffer_len =
      4096 * (sizeof(struct inotify_event) + NAME_MAX + 1);
  std::vector<char> buffer(buffer_len);

  struct StagedEvent {
    FileChangeEvent::Type type;
    std::chrono::steady_clock::time_point last_seen;
  };
  // Staging map to debounce rapid bursts from editors
  std::unordered_map<std::filesystem::path, StagedEvent> staging_events;

  while (m_Running.load()) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(m_INotifyFD, &fds);

    // Short 20ms timeout so we can frequently check quiet durations and
    // running_ state
    timeval timeout{.tv_sec = 0, .tv_usec = 20000};

    int ret = select(m_INotifyFD + 1, &fds, nullptr, nullptr, &timeout);
    if (ret < 0) {
      if (errno == EINTR)
        continue;
      break;
    }

    if (ret > 0) {
      ssize_t length = read(m_INotifyFD, buffer.data(), buffer.size());
      if (length > 0) {
        ssize_t i = 0;
        while (i < length) {
          auto *event = reinterpret_cast<struct inotify_event *>(&buffer[i]);

          if (event->len > 0) {
            std::string filename = event->name;
            std::filesystem::path dir_path;

            {
              std::lock_guard<std::mutex> lock(m_WDMutex);
              auto it = m_WatchDescriptors.find(event->wd);
              if (it != m_WatchDescriptors.end()) {
                dir_path = it->second;
              }
            }

            if (!dir_path.empty()) {
              std::filesystem::path full_path = dir_path / filename;

              if (event->mask & IN_ISDIR) {
                if (event->mask & (IN_CREATE | IN_MOVED_TO)) {
                  AddRecursiveWatches(full_path);
                }
              } else {
                if (full_path.extension() == ".cpp" ||
                    full_path.extension() == ".h") {
                  FileChangeEvent::Type type = FileChangeEvent::Modified;
                  if (event->mask & (IN_CREATE | IN_MOVED_TO))
                    type = FileChangeEvent::Created;
                  else if (event->mask & (IN_DELETE | IN_MOVED_FROM))
                    type = FileChangeEvent::Deleted;

                  // Stage or update the timestamp for debouncing
                  staging_events[full_path] = {
                      type, std::chrono::steady_clock::now()};
                }
              }
            }
          }
          i += sizeof(struct inotify_event) + event->len;
        }
      }
    }

    // Flush staged events that have been quiet for at least 150ms
    auto now = std::chrono::steady_clock::now();
    for (auto it = staging_events.begin(); it != staging_events.end();) {
      if (now - it->second.last_seen >= std::chrono::milliseconds(150)) {
        {
          std::lock_guard<std::mutex> lock(m_QueueMutex);
          m_PendingEvents.push({it->first, it->second.type});
        }
        it = staging_events.erase(it);
      } else {
        ++it;
      }
    }
  }
}
} // namespace Capybara
