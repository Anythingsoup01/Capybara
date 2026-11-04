#include "cpch.h"
#include "LibraryLoader.h"

#include <cstdint>
#include <cxxabi.h>
#include <elf.h>
#include <fcntl.h>
#include <gelf.h>
#include <libdwarf-2/libdwarf.h>
#include <libdwarf-2/dwarf.h>
#include <libelf.h>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <unordered_map>
#include <utility>

namespace Capybara {

static std::vector<std::string> s_NonUserDefines = {
  "std::",    "basic_ostream",         "type_info",   "typedef",   "_IO_",
  "__",       "_GLOBAL_OFFSET_TABLE_", "DW.",         "_fini",     "_init",
  "_DYNAMIC", "frame_dummy",           "completed.0", "deregister_tm_clones",
  "register_tm_clones",
};

LibraryLoader::LibraryLoader(const std::filesystem::path &libPath) {
  m_FD = open(libPath.c_str(), O_RDONLY);
  if (m_FD < 0) {
    throw std::runtime_error("Failed to open shared file!");
  }

  // Initialize Elf
  m_Elf = elf_begin(m_FD, ELF_C_READ, nullptr);
  if (!m_Elf) {
    close(m_FD);
    throw std::runtime_error("Failed to initialize libelft for file: " +
                             libPath.string() + " (" + elf_errmsg(-1) + ")");
  }

  // Initialize Dwarf
  Dwarf_Error dwErr = nullptr;
  int res =
      dwarf_init_b(m_FD, DW_GROUPNUMBER_ANY, nullptr, nullptr, &m_Dbg, &dwErr);
  if (res != DW_DLV_OK) {
    std::string err_msg = dwErr ? dwarf_errmsg(dwErr) : "Unknown DWARF Error!";
    if (dwErr) {
      dwarf_dealloc_error(m_Dbg, dwErr);
    }
    elf_end(m_Elf);
    close(m_FD);
    throw std::runtime_error("Failed to initialize libdwarf: " + err_msg);
  }

  ParseAllElfSymbols();
  ParseAllDwarfClasses();

  LoadSymbolsIntoClasses();
}

LibraryLoader::~LibraryLoader() {
  if (m_Dbg) {
    dwarf_finish(m_Dbg);
  }
  if (m_Elf) {
    elf_end(m_Elf);
  }
  if (m_FD >= 0) {
    close(m_FD);
  }
}

LibraryLoader::LibraryLoader(LibraryLoader &&other) noexcept
    : m_FD(std::exchange(other.m_FD, -1)),
      m_Elf(std::exchange(other.m_Elf, nullptr)),
      m_Dbg(std::exchange(other.m_Dbg, nullptr)) {}

LibraryLoader &LibraryLoader::operator=(LibraryLoader &&other) noexcept {
  if (this != &other) {
    if (m_Dbg)
      dwarf_finish(m_Dbg);
    if (m_Elf)
      elf_end(m_Elf);
    if (m_FD >= 0)
      close(m_FD);
    m_FD = std::exchange(other.m_FD, -1);
    m_Elf = std::exchange(other.m_Elf, nullptr);
    m_Dbg = std::exchange(other.m_Dbg, nullptr);
  }
  return *this;
}

void LibraryLoader::ParseAllElfSymbols() {
  std::unordered_map<std::string, SymbolInfo> symbols;
  Elf_Scn *scn = nullptr;

  while ((scn = elf_nextscn(m_Elf, scn)) != nullptr) {
    GElf_Shdr shdr;
    if (gelf_getshdr(scn, &shdr) != &shdr) {
      continue;
    }

    if (shdr.sh_type != SHT_SYMTAB)
      if (shdr.sh_type != SHT_DYNSYM)
        continue;

    Elf_Data *data = elf_getdata(scn, nullptr);
    if (!data)
      continue;

    int count = shdr.sh_size / shdr.sh_entsize;
    for (int i = 0; i < count; ++i) {
      GElf_Sym sym;
      if (gelf_getsym(data, i, &sym) != &sym) {
        continue;
      }

      unsigned char type = GELF_ST_TYPE(sym.st_info);
      unsigned char bind = GELF_ST_BIND(sym.st_info);

      if (type == STT_FILE || type == STT_SECTION || sym.st_shndx == SHN_UNDEF) {
        continue;
      }

      const char *name_ptr = elf_strptr(m_Elf, shdr.sh_link, sym.st_name);
      std::string name = name_ptr ? name_ptr : "";
      if (name.empty())
        continue;

      // Demangle the name
      int status = 0;
      char *res = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status);
      std::string demangled;
      if (status == 0 && res) {
        demangled = res;
        free(res);
      }

      // Swapped to demangled or name due to C standard names retaining no dema
      if (!IsUserDefined(demangled) || !IsUserDefined(name))
        continue;

      if (symbols.find(demangled) != symbols.end())
        continue;

      SymbolInfo info = {name,
                         demangled,
                         static_cast<uint64_t>(sym.st_value),
                         static_cast<uint64_t>(sym.st_size),
                         bind,
                         type};

      symbols.emplace(demangled, info);
    }
  }
  m_Symbols = symbols;
}

bool LibraryLoader::IsUserDefined(const std::string_view &in) const {
  for (const std::string &sysStr : s_NonUserDefines) {
    if (in.find(sysStr.c_str()) != std::string::npos) {
      return false;
    }
  }
  return true;
}

void LibraryLoader::ParseAllDwarfClasses() {
  std::unordered_map<std::string, ClassLayoutInfo> classes;
  if (!m_Dbg)
    return;

  // 1. Declare and initialize all output storage variables
  Dwarf_Bool is_info = 1; // 1 for .debug_info, 0 for .debug_types
  Dwarf_Error err = NULL;

  while (true) {
    int res = dwarf_next_cu_header_d(
        m_Dbg, is_info, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, &err);

    if (res == DW_DLV_NO_ENTRY)
      break;

    if (res == DW_DLV_ERROR) {
      if (err)
        dwarf_dealloc_error(m_Dbg, err);
      break;
    }

    Dwarf_Die cu_die = nullptr;
    res = dwarf_siblingof_b(m_Dbg, nullptr, is_info, &cu_die, &err);
    if (res == DW_DLV_OK && cu_die) {
      CollectClassesRecursive(cu_die, cu_die, classes);
      dwarf_dealloc(m_Dbg, cu_die, DW_DLA_DIE);
    }
  }
  m_Classes = classes;
}

void LibraryLoader::CollectClassesRecursive(Dwarf_Die die, Dwarf_Die cu_die, std::unordered_map<std::string, ClassLayoutInfo> &classes) const {
  Dwarf_Die current_die = die;

  while (current_die != nullptr) {
    Dwarf_Error error = nullptr;
    Dwarf_Half tag = 0;

    if (dwarf_tag(current_die, &tag, &error) == DW_DLV_OK &&
        (tag == DW_TAG_class_type || tag == DW_TAG_structure_type)) {
      if (IsUserDefinedDie(current_die, cu_die)) {
        char *name_ptr = nullptr;
        if (dwarf_diename(current_die, &name_ptr, &error) == DW_DLV_OK &&
            name_ptr) {
          ClassLayoutInfo info = ParseClassDie(current_die);
          classes.emplace(info.ClassName, info);
          dwarf_dealloc(m_Dbg, name_ptr, DW_DLA_STRING);
        }
      }
    } else if (error) {
      dwarf_dealloc_error(m_Dbg, error);
      error = nullptr;
    }

    // Traverse children
    if (tag == DW_TAG_compile_unit || tag == DW_TAG_namespace ||
        tag == DW_TAG_class_type || tag == DW_TAG_structure_type) {

      Dwarf_Die child = nullptr;
      if (dwarf_child(current_die, &child, &error) == DW_DLV_OK && child) {
        // Recurse down into children
        CollectClassesRecursive(child, cu_die, classes);
        dwarf_dealloc_die(child);
      }
    }

    if (error) {
      dwarf_dealloc_error(m_Dbg, error);
      error = nullptr;
    }

    // Advance to next sibling
    Dwarf_Die next_sibling = nullptr;
    int sib_res = dwarf_siblingof_c(current_die, &next_sibling, &error);

    if (current_die != die) {
      dwarf_dealloc_die(current_die);
    }

    if (sib_res == DW_DLV_OK && next_sibling) {
      current_die = next_sibling;
    } else {
      current_die = nullptr;
    }

    if (error) {
      dwarf_dealloc_error(m_Dbg, error);
      error = nullptr;
    }
  }
}

ClassLayoutInfo LibraryLoader::ParseClassDie(Dwarf_Die class_die) const {
  ClassLayoutInfo info;
  char *name_ptr = nullptr;
  Dwarf_Error error = nullptr;

  if (dwarf_diename(class_die, &name_ptr, &error) == DW_DLV_OK && name_ptr) {
    info.ClassName = name_ptr;
  } else if (error) {
    dwarf_dealloc_error(m_Dbg, error);
    error = nullptr;
  }

  // Total size via DW_AT_byte_size (Fixed: replaced Dwarf_Attribute type with DW_AT_byte_size constant)
  Dwarf_Attribute size_attr = nullptr;
  Dwarf_Unsigned byte_size = 0;
  if (dwarf_attr(class_die, DW_AT_byte_size, &size_attr, &error) == DW_DLV_OK) {
    if (dwarf_formudata(size_attr, &byte_size, &error) == DW_DLV_OK) {
      info.TotalSize = byte_size;
    } else if (error) {
      dwarf_dealloc_error(m_Dbg, error);
      error = nullptr;
    }
    dwarf_dealloc_attribute(size_attr);
  } else if (error) {
    dwarf_dealloc_error(m_Dbg, error);
    error = nullptr;
  }

  // Parse member fields (DW_TAG_member)
  Dwarf_Die child = nullptr;
  if (dwarf_child(class_die, &child, &error) == DW_DLV_OK && child) {
    Dwarf_Die current = child;
    while (true) {
      Dwarf_Half child_tag = 0;
      if (dwarf_tag(current, &child_tag, &error) == DW_DLV_OK) {
        if (child_tag == DW_TAG_member) {
          MemberInfo member = ParseMemberDie(current);
          info.Members.push_back(member);
        } else if (child_tag == DW_TAG_inheritance) {
          BaseClassInfo baseInfo{};

          // 1. Resolve base class type reference (DW_AT_type)
          Dwarf_Off base_type_offset = 0;
          Dwarf_Bool is_info = 1;
          if (dwarf_dietype_offset(child, &base_type_offset, &is_info, &error) == DW_DLV_OK) {
            Dwarf_Die base_type_die = FindDieByOffset(base_type_offset, is_info);
            if (base_type_die) {
              char *base_name_ptr = nullptr;
              if (dwarf_diename(base_type_die, &base_name_ptr, &error) == DW_DLV_OK && base_name_ptr) {
                baseInfo.ClassName = base_name_ptr;
                dwarf_dealloc(m_Dbg, base_name_ptr, DW_DLA_STRING);
              }
              dwarf_dealloc_die(base_type_die);
            }
          }

          // 2. Extract base class memory offset (DW_AT_data_member_location)
          Dwarf_Attribute loc_attr = nullptr;
          if (dwarf_attr(child, DW_AT_data_member_location, &loc_attr, &error) == DW_DLV_OK) {
            Dwarf_Unsigned uval = 0;
            if (dwarf_formudata(loc_attr, &uval, &error) == DW_DLV_OK) {
              baseInfo.ByteOffset = static_cast<uint32_t>(uval);
            }
            dwarf_dealloc_attribute(loc_attr);
          }

          info.BaseClasses.push_back(baseInfo);
        }
      } else if (error) {
        dwarf_dealloc_error(m_Dbg, error);
        error = nullptr;
      }

      Dwarf_Die next_sibling = nullptr;
      int res = dwarf_siblingof_c(current, &next_sibling, &error);

      // Deallocate intermediate sibling DIEs to prevent memory leaks
      if (current != child) {
        dwarf_dealloc_die(current);
      }

      if (res != DW_DLV_OK || !next_sibling) {
        if (res == DW_DLV_ERROR && error) {
          dwarf_dealloc_error(m_Dbg, error);
          error = nullptr;
        }
        break;
      }
      current = next_sibling;
    }
    // Deallocate the initial child DIE
    dwarf_dealloc_die(child);
  } else if (error) {
    dwarf_dealloc_error(m_Dbg, error);
    error = nullptr;
  }

  return info;
}

MemberInfo LibraryLoader::ParseMemberDie(Dwarf_Die member_die) const {
  MemberInfo member{};
  Dwarf_Error error = nullptr;

  // 1. Extract member name
  char *name_ptr = nullptr;
  if (dwarf_diename(member_die, &name_ptr, &error) == DW_DLV_OK && name_ptr) {
    member.Name = name_ptr;
    dwarf_dealloc(m_Dbg, name_ptr, DW_DLA_STRING);
  }
  if (error) {
    dwarf_dealloc_error(m_Dbg, error);
    error = nullptr;
  }

  // 2. Resolve DW_AT_type to get TypeName and SizeInBytes
  Dwarf_Off type_offset = 0;
  Dwarf_Bool is_info = 1;

  // Get the offset of the underlying type DIE
  if (dwarf_dietype_offset(member_die, &type_offset, &is_info, &error) ==
      DW_DLV_OK) {
    Dwarf_Die type_die = nullptr;

    // Fetch the type DIE using its offset
    if (dwarf_offdie_b(m_Dbg, type_offset, is_info, &type_die, &error) ==
            DW_DLV_OK &&
        type_die) {

      // Extract the Type Name (e.g., "int")
      char *type_name_ptr = nullptr;
      if (dwarf_diename(type_die, &type_name_ptr, &error) == DW_DLV_OK &&
          type_name_ptr) {
        member.TypeName = type_name_ptr;
        dwarf_dealloc(m_Dbg, type_name_ptr, DW_DLA_STRING);
      }
      if (error) {
        dwarf_dealloc_error(m_Dbg, error);
        error = nullptr;
      }

      // Extract the Type Size in Bytes (e.g., 4 for int)
      Dwarf_Unsigned byte_size = 0;
      if (dwarf_bytesize(type_die, &byte_size, &error) == DW_DLV_OK) {
        member.SizeInBytes = static_cast<uint32_t>(byte_size);
      }
      if (error) {
        dwarf_dealloc_error(m_Dbg, error);
        error = nullptr;
      }

      if (member.SizeInBytes == 0) {
        Dwarf_Unsigned fallback_size = 0;
        if (GetTypeByteSize(type_die, is_info, fallback_size)) {
          member.SizeInBytes = static_cast<uint32_t>(fallback_size);
        }
      }

      dwarf_dealloc_die(type_die);
    }
  }
  if (error) {
    dwarf_dealloc_error(m_Dbg, error);
    error = nullptr;
  }

  Dwarf_Attribute attr = nullptr;
  uint64_t byte_offset = 0;
  if (dwarf_attr(member_die, DW_AT_data_member_location, &attr, &error) ==
      DW_DLV_OK) {
    Dwarf_Half form = 0;
    if (dwarf_whatform(attr, &form, &error) == DW_DLV_OK) {

      // 1. Handle direct constant forms (e.g., DW_FORM_data1, DW_FORM_udata)
      if (form == DW_FORM_data1 || form == DW_FORM_data2 ||
          form == DW_FORM_data4 || form == DW_FORM_data8 ||
          form == DW_FORM_udata) {
        Dwarf_Unsigned uval = 0;
        if (dwarf_formudata(attr, &uval, &error) == DW_DLV_OK) {
          byte_offset = static_cast<uint64_t>(uval);
        }
      } else if (form == DW_FORM_sdata) {
        Dwarf_Signed sval = 0;
        if (dwarf_formsdata(attr, &sval, &error) == DW_DLV_OK) {
          byte_offset = static_cast<uint64_t>(sval);
        }
      }
      // 2. Handle expression/block forms (e.g., DW_FORM_exprloc, DW_FORM_block)
      else if (form == DW_FORM_exprloc || form == DW_FORM_block ||
               form == DW_FORM_block1 || form == DW_FORM_block2 ||
               form == DW_FORM_block4) {
        Dwarf_Block *block = nullptr;
        if (dwarf_formblock(attr, &block, &error) == DW_DLV_OK && block) {
          if (block->bl_len > 0 && block->bl_data) {
            // Cast bl_data from void* (Dwarf_Ptr) to unsigned char* for safe
            // indexing
            const uint8_t *data_bytes =
                reinterpret_cast<const uint8_t *>(block->bl_data);

            // Check if it uses DW_OP_plus_uconst (opcode 0x23)
            if (data_bytes[0] == 0x23 && block->bl_len > 1) {
              // Decode the ULEB128 offset following the opcode
              uint64_t value = 0;
              unsigned int shift = 0;
              for (Dwarf_Unsigned i = 1; i < block->bl_len; ++i) {
                uint8_t byte = data_bytes[i];
                value |= static_cast<uint64_t>(byte & 0x7F) << shift;
                if ((byte & 0x80) == 0) {
                  break; // End of ULEB128 sequence
                }
                shift += 7;
              }
              byte_offset = value;
            }
          }
        }
      }
    }
    dwarf_dealloc_attribute(attr);
  }

  if (error) {
    dwarf_dealloc_error(m_Dbg, error);
  }

  member.ByteOffset = byte_offset;

  return member;
}

bool LibraryLoader::IsUserDefinedDie(Dwarf_Die die, Dwarf_Die cu_die) const {
  // 1. Check Class Name Blacklist first (fastest and most reliable for
  // standard/compiler types)
  char *name_ptr = nullptr;
  Dwarf_Error error = nullptr;
  if (dwarf_diename(die, &name_ptr, &error) == DW_DLV_OK && name_ptr) {
    std::string className(name_ptr);
    dwarf_dealloc(m_Dbg, name_ptr, DW_DLA_STRING);

    // Filter out standard library, compiler internals, and runtime types
    if (!IsUserDefined(className))
      return false;
  }
  if (error) {
    dwarf_dealloc_error(m_Dbg, error);
  }

  // 2. Check Source File Path (Option 1)
  Dwarf_Attribute attr;
  Dwarf_Unsigned file_index = 0;

  if (dwarf_attr(die, DW_AT_decl_file, &attr, nullptr) == DW_DLV_OK) {
    if (dwarf_formudata(attr, &file_index, nullptr) == DW_DLV_OK) {
      char **srcfiles = nullptr;
      Dwarf_Signed file_count = 0;

      if (dwarf_srcfiles(cu_die, &srcfiles, &file_count, nullptr) ==
          DW_DLV_OK) {
        if (file_index > 0 && file_index <= (Dwarf_Unsigned)file_count) {
          std::string filename(srcfiles[file_index - 1]);

          for (Dwarf_Signed i = 0; i < file_count; ++i) {
            dwarf_dealloc(m_Dbg, srcfiles[i], DW_DLA_STRING);
          }
          dwarf_dealloc(m_Dbg, srcfiles, DW_DLA_LIST);

          // Reject any system paths or standard library include directories
          if (filename.find("/usr/include") != std::string::npos ||
              filename.find("/usr/lib/gcc") != std::string::npos ||
              filename.find("bits/") != std::string::npos ||
              filename.find("c++/") != std::string::npos) {
            return false;
          }
        }
      }
    }
  } else {
    // If it has no decl_file attribute at all, it's safer to reject it
    // unless you are certain it's user code.
    return false;
  }

  return true;
}

Dwarf_Die LibraryLoader::FindDieByOffset(Dwarf_Off offset, Dwarf_Bool is_info) const {
  Dwarf_Die target_die = nullptr;
  Dwarf_Error error = nullptr;

  // Properly passing the is_info boolean flag as the 3rd argument
  int res = dwarf_offdie_b(m_Dbg, offset, is_info, &target_die, &error);

  if (res == DW_DLV_OK) {
    return target_die;
  }
  return nullptr;
}

bool LibraryLoader::GetTypeByteSize(Dwarf_Die die, Dwarf_Bool is_info, Dwarf_Unsigned &out_size) const {
  Dwarf_Half tag = 0;
  if (dwarf_tag(die, &tag, nullptr) != DW_DLV_OK)
    return false;

  // Condition 1: Concrete structural target found
  if (tag == DW_TAG_class_type || tag == DW_TAG_structure_type) {
    Dwarf_Attribute attr;
    // Try reading byte size directly from DWARF
    if (dwarf_attr(die, DW_AT_byte_size, &attr, nullptr) == DW_DLV_OK) {
      dwarf_formudata(attr, &out_size, nullptr);
      dwarf_dealloc_attribute(attr);
      return true;
    }

    // Fallback for standard library stubs (like std::string or std::vector)
    // where DW_AT_byte_size is omitted by the compiler.
    char *name_ptr = nullptr;
    if (dwarf_diename(die, &name_ptr, nullptr) == DW_DLV_OK && name_ptr) {
      std::string typeName(name_ptr);
      dwarf_dealloc(m_Dbg, name_ptr, DW_DLA_STRING);

      if (typeName.find("basic_string") != std::string::npos) {
        out_size = 32; // libstdc++ string size on 64-bit
        return true;
      } else if (typeName.find("vector") != std::string::npos) {
        out_size = 24; // standard vector size on 64-bit
        return true;
      }
    }

    return false;
  }

  // Condition 2: Follow references across qualifiers or aliases (typedef,
  // const, volatile)
  if (tag == DW_TAG_typedef || tag == DW_TAG_const_type ||
      tag == DW_TAG_volatile_type) {
    Dwarf_Attribute attr;
    if (dwarf_attr(die, DW_AT_type, &attr, nullptr) == DW_DLV_OK) {
      Dwarf_Off ref_offset = 0;
      Dwarf_Half form = 0;
      int ref_res = DW_DLV_ERROR;
      Dwarf_Error error = nullptr;

      // Check the form to use the correct reference extraction function
      if (dwarf_whatform(attr, &form, &error) == DW_DLV_OK) {
        if (form == DW_FORM_ref_addr) {
          ref_res = dwarf_global_formref(attr, &ref_offset, &error);
        } else {
          // Standard DW_AT_type uses local CU-relative reference forms
          ref_res = dwarf_formref(attr, &ref_offset, &is_info, &error);
        }
      }
      if (error) {
        dwarf_dealloc_error(m_Dbg, error);
      }

      dwarf_dealloc_attribute(attr);

      if (ref_res == DW_DLV_OK) {
        Dwarf_Bool is_info = dwarf_get_die_infotypes_flag(die);
        Dwarf_Die next_die = FindDieByOffset(ref_offset, is_info);
        if (next_die) {
          bool success = GetTypeByteSize(next_die, is_info, out_size);
          dwarf_dealloc_die(next_die);
          return success;
        }
      }
    }
  }
  return false;
}

void LibraryLoader::LoadSymbolsIntoClasses() {
  for (auto &[name, sym] : m_Symbols) {
    // 1. Strip the parameter list (everything from '(' onwards)
    size_t paramPos = name.find('(');
    std::string scopeAndMethod =
        (paramPos != std::string::npos) ? name.substr(0, paramPos) : name;

    // 2. Find the last '::' to separate the method name from its scope
    size_t lastColonPos = scopeAndMethod.rfind("::");

    if (lastColonPos != std::string::npos) {
      std::string fullParentPath = scopeAndMethod.substr(0, lastColonPos);
      std::string methodName = scopeAndMethod.substr(lastColonPos + 2);

      // 3. Extract the immediate enclosing scope (Class name or innermost
      // namespace)
      size_t prevColonPos = fullParentPath.rfind("::");
      std::string classNameOrScope = "";
      std::string outerNamespace = "";

      if (prevColonPos != std::string::npos) {
        // There are multiple levels (e.g., "Namespace::Class")
        classNameOrScope = fullParentPath.substr(prevColonPos + 2);
        outerNamespace = fullParentPath.substr(0, prevColonPos);
      } else {
        // Only one level exists (e.g., "Class::method" or
        // "Namespace::function")
        classNameOrScope = fullParentPath;
        outerNamespace = "";
      }

      if (m_Classes.find(classNameOrScope) != m_Classes.end()) { // Class exists
        auto &classInfo = m_Classes.at(classNameOrScope);
        if (classNameOrScope == methodName) { // Constructor
          classInfo.ConstructorName = sym.Name;
          continue;
        }

        if (methodName.find('~', 0) != std::string::npos) {
          std::string sub = methodName.substr(1);
          if (sub == classInfo.ClassName) {
            classInfo.DestructorName = sym.Name;
            continue;
          }
        }
        SymbolInfo info = sym;
        info.DemangledName = methodName;
        m_Classes.at(classNameOrScope).MethodSymbols.push_back(info);
      } else {
        methodName = scopeAndMethod;
        SymbolInfo info = sym;
        info.DemangledName = methodName;
        m_GlobalSymbols.push_back(info);
      }

      // Example breakdown for "A::B::MyClass::myMethod":
      // outerNamespace       -> "A::B"
      // classNameOrScope     -> "MyClass"
      // methodName           -> "myMethod"
    } else {
      // Global scope function
      std::string methodName = scopeAndMethod;
      SymbolInfo info = sym;
      info.DemangledName = methodName;
      m_GlobalSymbols.push_back(info);
    }
  }
}

} // namespace Capybara
