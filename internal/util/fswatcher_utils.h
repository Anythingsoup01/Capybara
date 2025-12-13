#pragma once

// This is the default internal functionality for file creation
void fswatcher_on_create(const std::filesystem::path& path);

// This is the default internal functionality for file modification
void fswatcher_on_modified(const std::filesystem::path& path);

// This is the default internal functionality for file deletion
void fswatcher_on_deleted(const std::filesystem::path& path);
