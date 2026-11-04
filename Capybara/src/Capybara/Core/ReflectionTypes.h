#pragma once

#include "Capybara.h"
#include <cstdint>
#include <memory>
#include <vector>

#include <ffi.h>


namespace Capybara {


// Metadata structs
struct SymbolInfo {
  std::string Name;
  std::string DemangledName;
  uint64_t Value;
  uint64_t Size;
  unsigned char Bind;
  unsigned char Type;
};

struct MemberInfo {
  std::string Name;
  std::string TypeName;
  uint64_t ByteOffset = 0;
  uint64_t SizeInBytes = 0;
};

struct BaseClassInfo {
    std::string ClassName;
    uint32_t ByteOffset = 0; // Offset of the base subobject within the derived class
};

// For classes
struct ClassLayoutInfo {
  std::string ClassName;
  uint64_t TotalSize = 0;
  std::string ConstructorName;
  std::string DestructorName;
  std::vector<BaseClassInfo> BaseClasses;
  std::vector<MemberInfo> Members;
  std::vector<SymbolInfo> MethodSymbols;
};

// Reflection Structs

struct FieldInfo {
  std::string Name;
  std::string TypeName;
  uint64_t ByteOffset;
  uint64_t SizeInBytes;
};

struct MethodInfo {
  std::string Name;
  void *SymbolPointer = nullptr; // resolved by dlsym
};

struct CapyObject {
  std::string ClassName;
  uint64_t TotalSize = 0;
  std::vector<BaseClassInfo> BaseClasses;
  std::vector<FieldInfo> Fields;
  std::vector<MethodInfo> Methods;
  void *ConstructorPointer = nullptr;
  void *DestructorPointer = nullptr;

  struct Instance {
    std::vector<uint8_t> RawMemory;

    template <typename T>
    T *GetField(const std::string &fieldName, const CapyObject *classDef) {
      for (const auto &field : classDef->Fields) {
        if (fieldName == field.Name) {
          if (RawMemory.size() < field.ByteOffset + sizeof(T))
            return nullptr;
          return reinterpret_cast<T *>(RawMemory.data() + field.ByteOffset);
        }
      }

      for (const auto &baseClassInfo : classDef->BaseClasses) {
        auto baseClassDef = Capybara::GetClass(baseClassInfo.ClassName);
        if (baseClassDef) {
          for (const auto &baseField : baseClassDef->Fields) {
            if (baseField.Name == fieldName) {
              uint32_t absoluteOffset = baseClassInfo.ByteOffset + baseField.ByteOffset;
              if (RawMemory.size() < absoluteOffset + sizeof(T)) return nullptr;
              return reinterpret_cast<T*>(RawMemory.data() + absoluteOffset);
            }
          }
        }
      }

      return nullptr;
    }

    template <typename T>
    bool SetField(const std::string &fieldName, const T &value, const CapyObject *classDef) {
      T *fieldPtr = GetField<T>(fieldName, classDef);
      if (!fieldPtr) {
        std::cerr << "Capybara Error: Field '" << fieldName 
          << "' not found or type mismatch on class '" << classDef->ClassName << "'!\n";
        return false;
      }

      *fieldPtr = value;
      return true;
    }

    // Method Ivoking
    template <typename Ret, typename... Args>
    Ret Invoke(const std::string &methodName, const CapyObject *classDef,
               Args &&...args) {
      void *symbolPointer = nullptr;
      for (const auto &method : classDef->Methods) {
        if (method.Name == methodName) {
          symbolPointer = method.SymbolPointer;
          break;
        }
      }

      if (!symbolPointer) {
        std::cerr << "Capybara Error: Method '" << methodName
                  << "' not found in class '" << classDef->ClassName << "'!"
                  << std::endl;
        if constexpr (!std::is_void_v<Ret>) {
          return Ret{};
        }
        return;
      }

      // 1. Prepare libffi argument types and values arrays
      // Note: We add +1 for the hidden 'this' pointer as the first argument.
      constexpr size_t numArgs = sizeof...(Args);
      ffi_type *argTypes[numArgs + 1];
      void *argValues[numArgs + 1];

      // The first argument to any non-static member function is the 'this'
      // pointer
      argTypes[0] = &ffi_type_pointer;
      void *thisPtr = RawMemory.data();
      argValues[0] = &thisPtr;

      // Pack the remaining arguments
      // We use a helper lambda or tuple expansion to populate argTypes and
      // argValues
      int index = 1;
      ((argTypes[index] = GetFfiType<Args>(),
        argValues[index] = const_cast<void *>(static_cast<const void *>(&args)),
        ++index),
       ...);

      // 2. Determine return type mapping for libffi
      ffi_type *rtype = GetFfiType<Ret>();

      // 3. Prepare the Call Interface (CIF)
      ffi_cif cif;
      if (ffi_prep_cif(&cif, FFI_DEFAULT_ABI, numArgs + 1, rtype, argTypes) !=
          FFI_OK) {
        std::cerr << "Capybara Error: ffi_prep_cif failed!\n";
        if constexpr (!std::is_void_v<Ret>)
          return Ret{};
        return;
      }

      // 4. Allocate storage for the return value if non-void
      using RawRetType = std::conditional_t<std::is_void_v<Ret>, int, Ret>;
      RawRetType retValue{};

      // 5. Execute the call safely across ABIs
      ffi_call(&cif, FFI_FN(symbolPointer), &retValue, argValues);

      if constexpr (!std::is_void_v<Ret>) {
        return retValue;
      }
    }

  private:
    // Helper mapping C++ types to libffi descriptors
    template <typename T> ffi_type *GetFfiType() {
      using CleanT = std::remove_cv_t<std::remove_reference_t<T>>;

      if constexpr (std::is_pointer_v<T> || std::is_reference_v<T>) {
        return &ffi_type_pointer;
      } else if constexpr (std::is_same_v<CleanT, int>) {
        return &ffi_type_sint32;
      } else if constexpr (std::is_same_v<CleanT, unsigned int>) {
        return &ffi_type_uint32;
      } else if constexpr (std::is_same_v<CleanT, float>) {
        return &ffi_type_float;
      } else if constexpr (std::is_same_v<CleanT, double>) {
        return &ffi_type_double;
      } else if constexpr (std::is_same_v<CleanT, bool>) {
        return &ffi_type_uint8;
      } else if constexpr (std::is_void_v<CleanT>) {
        return &ffi_type_void;
      } else {
        // Default fallback for complex structs/objects passed by pointer/ref
        return &ffi_type_pointer;
      }
    }
  };

  std::shared_ptr<Instance> CreateInstance() {
    auto instance = std::make_shared<Instance>();
    instance->RawMemory.resize(TotalSize, 0);

    if (ConstructorPointer) {
      ((void (*)(void *))(ConstructorPointer))(instance->RawMemory.data());
    }

    m_Instances.push_back(instance);
    return instance;
  }

  const std::vector<std::shared_ptr<Instance>> &GetInstances() const {
    return m_Instances;
  }

  void ClearInstances() { m_Instances.clear(); }

private:
  std::vector<std::shared_ptr<Instance>> m_Instances;
};

} // namespace Capybara
