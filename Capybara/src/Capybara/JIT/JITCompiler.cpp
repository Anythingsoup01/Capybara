#include "Capybara/cpch.h"
#include <fstream>
#include "JITCompiler.h"

namespace Capybara {

bool JitCompiler::ProcessChanges(const std::vector<FileChangeEvent> &changes) {
  if (changes.empty()) return false;

  bool needRelink = false;
  bool sourceTopologyChanged = true;

  for (const auto &change : changes) {
    if (change.Path.extension() != ".cpp")
      continue;
    std::filesystem::path objPath = GetObjectPath(change.Path);

    if (change.type == FileChangeEvent::Deleted) {
      if (std::filesystem::exists(objPath)) {
        std::filesystem::remove(objPath);
      }
      needRelink = true;
      sourceTopologyChanged = true;
    } else {
      if (change.type == FileChangeEvent::Created) {
        sourceTopologyChanged = true;
      }

      if (CompileSingleFile(change.Path, objPath)) {
        needRelink = true;
      }
    }
  }

  CleanupOrphanBinaries();

  if (sourceTopologyChanged) {
    GenerateCompileCommands();
  }

  if (needRelink) {
    return LinkAll();
  }

  return false;
}

std::filesystem::path JitCompiler::GetObjectPath(const std::filesystem::path &srcPath) {
  std::filesystem::path rel = std::filesystem::relative(srcPath, m_SourcePath);
  std::string flat_name = rel.string();

  // Replace directory separators with underscores
  for (char& c : flat_name) {
    if (c == '/' || c == '\\') {
      c = '_';
    }
  }

  // Replace extension with .o
  if (rel.extension() == ".cpp" || rel.extension() == ".cc" || rel.extension() == ".c") {
    flat_name = flat_name.substr(0, flat_name.rfind('.')) + ".o";
  } else {
    flat_name += ".o";
  }

  return m_BuildDir / flat_name;
}

bool JitCompiler::CompileSingleFile(const std::filesystem::path &srcPath, const std::filesystem::path &objPath) {
  std::string cmd = "g++ -c \"" + srcPath.string() +
                    "\" -o \""  + objPath.string() +
                    "\" -std=c++20 -fPIC -gdwarf-4";

  if (!m_ExternalBinaryPath.empty() && !m_ExternalIncludePath.empty())
    cmd += " -I\"" + m_ExternalIncludePath.string() + "\"";

  int result = std::system(cmd.c_str());
  return (result == 0);
}

void JitCompiler::CleanupOrphanBinaries() {
  // Build a set of valid expected object paths based on current source files
  std::unordered_set<std::filesystem::path> valid_objects;
  for (auto const& entry : std::filesystem::recursive_directory_iterator(m_SourcePath)) {
    if (entry.is_regular_file()) {
      auto ext = entry.path().extension();
      if (ext == ".cpp" || ext == ".cc" || ext == ".c") {
        valid_objects.insert(GetObjectPath(entry.path()));
      }
    }
  }

  // Scan build directory and remove any .o files not present in valid_objects
  for (auto const& entry : std::filesystem::directory_iterator(m_SourcePath)) {
    if (entry.is_regular_file() && entry.path().extension() == ".o") {
      if (valid_objects.find(entry.path()) == valid_objects.end()) {
        std::filesystem::remove(entry.path());
      }
    }
  }
}

void JitCompiler::GenerateCompileCommands() {
  std::filesystem::path json_path = m_SourcePath / "compile_commands.json";
  std::ofstream json_file(json_path);
  if (!json_file.is_open()) {
    std::cerr << "[JitCompiler] Warning: Could not write compile_commands.json to " << json_path << "\n";
    return;
  }

  json_file << "[\n";
  bool first = true;

  for (auto const& entry : std::filesystem::recursive_directory_iterator(m_SourcePath)) {
    if (entry.is_regular_file()) {
      auto ext = entry.path().extension();
      if (ext == ".cpp" || ext == ".cc" || ext == ".c") {
        std::filesystem::path abs_src = std::filesystem::absolute(entry.path());
        std::filesystem::path abs_obj = GetObjectPath(abs_src);

        std::string command = "g++ -c " + abs_src.string() + 
          " -o " + abs_obj.string() + 
          " -std=c++20 -fPIC -O2" +
          " -I" + std::filesystem::absolute(m_ExternalIncludePath).string();

        if (!first) {
          json_file << ",\n";
        }
        first = false;

        json_file << "  {\n";
        json_file << "    \"directory\": \"" << m_SourcePath.string() << "\",\n";
        json_file << "    \"file\": \"" << abs_src.string() << "\",\n";
        // Escape quotes inside command if necessary
        json_file << "    \"command\": \"" << command << "\"\n";
        json_file << "  }";
      }
    }
  }

  json_file << "\n]\n";
}

bool JitCompiler::LinkAll() {
  // Collect all compiled object files in the build directory
  std::string obj_list = "";
  int count = 0;
  for (auto const& entry : std::filesystem::directory_iterator(m_BuildDir)) {
    if (entry.is_regular_file() && entry.path().extension() == ".o") {
      obj_list += "\"" + entry.path().string() + "\" ";
      count++;
    }
  }

  if (count == 0) {
    return false;
  }

  std::filesystem::path output_lib = m_BuildDir / "CapyRuntimeLib.so";

  // Link command: g++ -shared -o libscript_hotload.so [objects] [script_core]
  std::string cmd = "g++ -shared -o \"" + output_lib.string() + "\" " + obj_list;

  if (!m_ExternalBinaryPath.empty() && !m_ExternalIncludePath.empty())
    cmd += " \"" + m_ExternalBinaryPath.string() + "\"";


  int result = std::system(cmd.c_str());
  if (result == 0) {
    return true;
  } else {
    return false;
  }
}

}
