#include "Capybara/cpch.h"
#include "ObjectAllocator.h"

#include "ReflectionTypes.h"

#include <dlfcn.h>

namespace Capybara {

std::shared_ptr<CapyObject> ObjectAllocator::CreateCapyObject(void *dlHandle, const ClassLayoutInfo &layout) {
  auto obj = std::make_shared<CapyObject>();
  obj->ClassName = layout.ClassName;
  obj->TotalSize = layout.TotalSize;
  obj->BaseClasses = layout.BaseClasses;

  // Copy Fields
  for (const auto &field : layout.Members) {
    FieldInfo info;
    info.Name = field.Name;
    info.ByteOffset = field.ByteOffset;
    info.SizeInBytes = field.SizeInBytes;
    info.TypeName = field.TypeName;
    obj->Fields.push_back(info);
  }

  // Initialize Methods
  for (const auto &method : layout.MethodSymbols) {
    MethodInfo info;
    info.Name = method.DemangledName;
    info.SymbolPointer = dlsym(dlHandle, method.Name.c_str());
    if (!info.SymbolPointer) {
      std::cerr << "ObjectAllocator WARNING: "  << dlerror() << "\n";
    }
    obj->Methods.push_back(std::move(info));
  }

  obj->ConstructorPointer = dlsym(dlHandle, layout.ConstructorName.c_str());
  if (obj->ConstructorPointer) {
    auto instance = std::make_shared<CapyObject::Instance>();
    instance->RawMemory.resize(layout.TotalSize, 0);

    // 1. Run constructor to set default member values in memory
    ((void (*)(void *))(obj->ConstructorPointer))(instance->RawMemory.data());

    // 2. Extract and store default values safely
    for (auto &field : obj->Fields) {
      field.DefaultValue.resize(field.SizeInBytes, 0); // Allocate buffer
      instance->GetFieldVoid(field.Name, obj.get(), field.DefaultValue.data(), field.SizeInBytes);
    }

    // 3. Clean up: Run destructor on the temporary instance before discarding it
    if (layout.DestructorName.size() > 0) {
      obj->DestructorPointer = dlsym(dlHandle, layout.DestructorName.c_str());
      if (obj->DestructorPointer) {
        ((void (*)(void *))(obj->DestructorPointer))(instance->RawMemory.data());
      }
    }
  }

  dlerror();

  return obj;
}

}
