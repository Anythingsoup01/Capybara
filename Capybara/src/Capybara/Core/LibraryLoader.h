#pragma once

#include <libelf.h>
#include <libdwarf-2/libdwarf.h>

#include "ReflectionTypes.h"

namespace Capybara {

class LibraryLoader {
public:
  LibraryLoader() = default;
  explicit LibraryLoader(const std::filesystem::path &libPath);
  ~LibraryLoader();

  // No copying or moving this class, especially since it holds file info and an
  // Elf pointer
  LibraryLoader(const LibraryLoader &) = delete;
  LibraryLoader &operator=(const LibraryLoader &) = delete;

  LibraryLoader(LibraryLoader &&other) noexcept;
  LibraryLoader &operator=(LibraryLoader &&other) noexcept;

  const std::unordered_map<std::string, ClassLayoutInfo> &GetClasses() const { return m_Classes; }
private:

  // Finishing Touch
  void LoadSymbolsIntoClasses();

  // Internal DWARF recursive helpers
  void ParseAllDwarfClasses();
  void CollectClassesRecursive(Dwarf_Die die, Dwarf_Die cu_die, std::unordered_map<std::string, ClassLayoutInfo>& classes) const;
  ClassLayoutInfo ParseClassDie(Dwarf_Die class_die) const;
  MemberInfo ParseMemberDie(Dwarf_Die member_die) const;
  bool IsUserDefinedDie(Dwarf_Die die, Dwarf_Die cu_die) const;
  Dwarf_Die FindDieByOffset(Dwarf_Off offset, Dwarf_Bool is_info) const;
  bool GetTypeByteSize(Dwarf_Die die, Dwarf_Bool is_info, Dwarf_Unsigned &out_size) const;

  // Internal Elf helpers
  void ParseAllElfSymbols();

  bool IsUserDefined(const std::string_view &in) const;
private:
  int m_FD;
  Elf *m_Elf = nullptr;
  Dwarf_Debug m_Dbg = nullptr;

  std::unordered_map<std::string, SymbolInfo> m_Symbols;
  std::unordered_map<std::string, ClassLayoutInfo> m_Classes;
  std::vector<SymbolInfo> m_GlobalSymbols;
};

} // namespace Capybara
