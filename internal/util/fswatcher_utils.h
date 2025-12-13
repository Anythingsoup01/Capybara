#pragma once

void fswatcher_on_create(const std::filesystem::path& path);

void fswatcher_on_modified(const std::filesystem::path& path);

void fswatcher_on_deleted(const std::filesystem::path& path);
