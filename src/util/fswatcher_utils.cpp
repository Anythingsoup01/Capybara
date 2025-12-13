#include "cpypch.h"
#include "util/fswatcher_utils.h"

#include "capybara/runtime.h"

#include "util/string_util.h"


bool check_if_supported_extension(const std::filesystem::path& path)
{
    std::string extension = path.extension().string();

    if (extension.find("~") != std::string::npos)
        return false;

    if (!strs_n_equal(extension, {".c", ".cpp", ".h", ".hpp"}))
        return false;

    return true;

}

void fswatcher_on_create(const std::filesystem::path& path)
{
    if (!check_if_supported_extension(path))
        return;

    std::lock_guard<std::mutex> lock(s_Storage.JITStorage.PendingFileMutex);
    s_Storage.JITStorage.PendingFiles.push_back(path);
}

void fswatcher_on_modified(const std::filesystem::path& path)
{
    if (!check_if_supported_extension(path))
        return;

    std::lock_guard<std::mutex> lock(s_Storage.JITStorage.PendingFileMutex);
    s_Storage.JITStorage.PendingFiles.push_back(path);

}

void fswatcher_on_deleted(const std::filesystem::path& path)
{
    if (!check_if_supported_extension(path))
        return;
}
