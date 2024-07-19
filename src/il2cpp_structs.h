#pragma once

#include <cstdint>
#include <cstddef>

// See: https://github.com/nneonneo/Il2CppVersions/blob/master/headers/2022.3.5f1.h
namespace il2cpp
{

// -------------------------
// - METADATA
// -------------------------

struct Il2CppGlobalMetadataHeader {
  int32_t sanity;
  int32_t version;
  int32_t stringLiteralOffset; // string data for managed code
  int32_t stringLiteralSize;
  int32_t stringLiteralDataOffset;
  int32_t stringLiteralDataSize;
  int32_t stringOffset; // string data for metadata
  int32_t stringSize;
  int32_t eventsOffset; // Il2CppEventDefinition
  int32_t eventsSize;
  int32_t propertiesOffset; // Il2CppPropertyDefinition
  int32_t propertiesSize;
  int32_t methodsOffset; // Il2CppMethodDefinition
  int32_t methodsSize;
  int32_t parameterDefaultValuesOffset; // Il2CppParameterDefaultValue
  int32_t parameterDefaultValuesSize;
  int32_t fieldDefaultValuesOffset; // Il2CppFieldDefaultValue
  int32_t fieldDefaultValuesSize;
  int32_t fieldAndParameterDefaultValueDataOffset; // uint8_t
  int32_t fieldAndParameterDefaultValueDataSize;
  int32_t fieldMarshaledSizesOffset; // Il2CppFieldMarshaledSize
  int32_t fieldMarshaledSizesSize;
  int32_t parametersOffset; // Il2CppParameterDefinition
  int32_t parametersSize;
  int32_t fieldsOffset; // Il2CppFieldDefinition
  int32_t fieldsSize;
  int32_t genericParametersOffset; // Il2CppGenericParameter
  int32_t genericParametersSize;
  int32_t genericParameterConstraintsOffset; // TypeIndex
  int32_t genericParameterConstraintsSize;
  int32_t genericContainersOffset; // Il2CppGenericContainer
  int32_t genericContainersSize;
  int32_t nestedTypesOffset; // TypeDefinitionIndex
  int32_t nestedTypesSize;
  int32_t interfacesOffset; // TypeIndex
  int32_t interfacesSize;
  int32_t vtableMethodsOffset; // EncodedMethodIndex
  int32_t vtableMethodsSize;
  int32_t interfaceOffsetsOffset; // Il2CppInterfaceOffsetPair
  int32_t interfaceOffsetsSize;
  int32_t typeDefinitionsOffset; // Il2CppTypeDefinition
  int32_t typeDefinitionsSize;

  // We don't necessarily need these, plus these are Il2Cpp version dependent
#if 0
  int32_t imagesOffset;
  int32_t imagesSize;
  int32_t assembliesOffset;
  int32_t assembliesSize;
  int32_t fieldRefsOffset;
  int32_t fieldRefsSize;
  int32_t referencedAssembliesOffset;
  int32_t referencedAssembliesSize;
  int32_t attributeDataOffset;
  int32_t attributeDataSize;
  int32_t attributeDataRangeOffset;
  int32_t attributeDataRangeSize;
  int32_t unresolvedIndirectCallParameterTypesOffset;
  int32_t unresolvedIndirectCallParameterTypesSize;
  int32_t unresolvedIndirectCallParameterRangesOffset;
  int32_t unresolvedIndirectCallParameterRangesSize;
  int32_t windowsRuntimeTypeNamesOffset;
  int32_t windowsRuntimeTypeNamesSize;
  int32_t windowsRuntimeStringsOffset;
  int32_t windowsRuntimeStringsSize;
  int32_t exportedTypeDefinitionsOffset;
  int32_t exportedTypeDefinitionsSize;
#endif
};

struct Il2CppTypeDefinition {
  uint32_t nameIndex;
  uint32_t namespaceIndex;
  int32_t byvalTypeIndex;
  int32_t declaringTypeIndex;
  int32_t parentIndex;
  int32_t elementTypeIndex;
  int32_t genericContainerIndex;
  uint32_t flags;
  int32_t fieldStart;
  int32_t methodStart;
  int32_t eventStart;
  int32_t propertyStart;
  int32_t nestedTypesStart;
  int32_t interfacesStart;
  int32_t vtableStart;
  int32_t interfaceOffsetsStart;
  uint16_t method_count;
  uint16_t property_count;
  uint16_t field_count;
  uint16_t event_count;
  uint16_t nested_type_count;
  uint16_t vtable_count;
  uint16_t interfaces_count;
  uint16_t interface_offsets_count;

  // bitfield to portably encode boolean values as single bits
  // 01 - valuetype;
  // 02 - enumtype;
  // 03 - has_finalize;
  // 04 - has_cctor;
  // 05 - is_blittable;
  // 06 - is_import_or_windows_runtime;
  // 07-10 - One of nine possible PackingSize values (0, 1, 2, 4, 8, 16, 32, 64, or 128)
  // 11 - PackingSize is default
  // 12 - ClassSize is default
  // 13-16 - One of nine possible PackingSize values (0, 1, 2, 4, 8, 16, 32, 64, or 128) - the specified packing size
  // (even for explicit layouts)
  uint32_t bitfield;
  uint32_t token;
};

// -------------------------
// - RUNTIME
// -------------------------

struct VirtualInvokeData {
  uintptr_t methodPtr; // Il2CppMethodPointer
  uintptr_t method;    // const MethodInfo*
};

enum class Il2CppTypeEnum {
  IL2CPP_TYPE_END = 0x00,
  IL2CPP_TYPE_VOID = 0x01,
  IL2CPP_TYPE_BOOLEAN = 0x02,
  IL2CPP_TYPE_CHAR = 0x03,
  IL2CPP_TYPE_I1 = 0x04,
  IL2CPP_TYPE_U1 = 0x05,
  IL2CPP_TYPE_I2 = 0x06,
  IL2CPP_TYPE_U2 = 0x07,
  IL2CPP_TYPE_I4 = 0x08,
  IL2CPP_TYPE_U4 = 0x09,
  IL2CPP_TYPE_I8 = 0x0a,
  IL2CPP_TYPE_U8 = 0x0b,
  IL2CPP_TYPE_R4 = 0x0c,
  IL2CPP_TYPE_R8 = 0x0d,
  IL2CPP_TYPE_STRING = 0x0e,
  IL2CPP_TYPE_PTR = 0x0f,
  IL2CPP_TYPE_BYREF = 0x10,
  IL2CPP_TYPE_VALUETYPE = 0x11,
  IL2CPP_TYPE_CLASS = 0x12,
  IL2CPP_TYPE_VAR = 0x13,
  IL2CPP_TYPE_ARRAY = 0x14,
  IL2CPP_TYPE_GENERICINST = 0x15,
  IL2CPP_TYPE_TYPEDBYREF = 0x16,
  IL2CPP_TYPE_I = 0x18,
  IL2CPP_TYPE_U = 0x19,
  IL2CPP_TYPE_FNPTR = 0x1b,
  IL2CPP_TYPE_OBJECT = 0x1c,
  IL2CPP_TYPE_SZARRAY = 0x1d,
  IL2CPP_TYPE_MVAR = 0x1e,
  IL2CPP_TYPE_CMOD_REQD = 0x1f,
  IL2CPP_TYPE_CMOD_OPT = 0x20,
  IL2CPP_TYPE_INTERNAL = 0x21,
  IL2CPP_TYPE_MODIFIER = 0x40,
  IL2CPP_TYPE_SENTINEL = 0x41,
  IL2CPP_TYPE_PINNED = 0x45,
  IL2CPP_TYPE_ENUM = 0x55,
  IL2CPP_TYPE_IL2CPP_TYPE_INDEX = 0xff
};

struct Il2CppArrayType {
  uintptr_t etype; // const Il2CppType*
  uint8_t rank;
  uint8_t numsizes;
  uint8_t numlobounds;
  int* sizes;
  int* lobounds;
};

struct Il2CppGenericInst {
  uint32_t type_argc;
  uintptr_t type_argv; // const Il2CppType**
};

struct Il2CppGenericContext {
  uintptr_t class_inst;  // const Il2CppGenericInst*
  uintptr_t method_inst; // const Il2CppGenericInst*
};

struct Il2CppGenericClass {
  uintptr_t type; // const Il2CppType*
  Il2CppGenericContext context;
  uintptr_t cached_class; // Il2CppClass*
};

struct Il2CppType {
#if 0
  union {
    void* dummy;
    TypeDefinitionIndex __klassIndex;
    Il2CppMetadataTypeHandle typeHandle;
    const Il2CppType* type;
    Il2CppArrayType* array;
    GenericParameterIndex __genericParameterIndex;
    Il2CppMetadataGenericParameterHandle genericParameterHandle;
    Il2CppGenericClass* generic_class;
  } data;
#else
  uintptr_t data;
#endif
  unsigned int attrs : 16;
  Il2CppTypeEnum type : 8;
  unsigned int num_mods : 5;
  unsigned int byref : 1;
  unsigned int pinned : 1;
  unsigned int valuetype : 1;
};

struct FieldInfo {
  uintptr_t name;   // const char*
  uintptr_t type;   // const Il2CppType*
  uintptr_t parent; // Il2CppClass*
  int32_t offset;
  uint32_t token;
};

struct Il2CppClass {
  uintptr_t image;     // const Il2CppImage*
  uintptr_t gc_desc;   // void*
  uintptr_t name;      // const char*
  uintptr_t namespaze; // const char*
  Il2CppType byval_arg;
  Il2CppType this_arg;
  uintptr_t element_class;         // Il2CppClass*
  uintptr_t castClass;             // Il2CppClass*
  uintptr_t declaringType;         // Il2CppClass*
  uintptr_t parent;                // Il2CppClass*
  uintptr_t generic_class;         // Il2CppGenericClass*
  uintptr_t typeMetadataHandle;    // Il2CppMetadataTypeHandle
  uintptr_t interopData;           // const Il2CppInteropData*
  uintptr_t klass;                 // Il2CppClass*
  uintptr_t fields;                // FieldInfo*
  uintptr_t events;                // const EventInfo*
  uintptr_t properties;            // const PropertyInfo*
  uintptr_t methods;               // const MethodInfo**
  uintptr_t nestedTypes;           // Il2CppClass**
  uintptr_t implementedInterfaces; // Il2CppClass**
  uintptr_t interfaceOffsets;      // Il2CppRuntimeInterfaceOffsetPair*
  uintptr_t static_fields; // struct [CLASS]_StaticFields*
  uintptr_t rgctx_data;    // const Il2CppRGCTXData*
  uintptr_t typeHierarchy;   // struct Il2CppClass**
  uintptr_t unity_user_data; // void*
  uint32_t initializationExceptionGCHandle;
  uint32_t cctor_started;
  uint32_t cctor_finished_or_no_cctor;
  size_t cctor_thread;
  uintptr_t genericContainerHandle; // void*
  uint32_t instance_size;
  uint32_t stack_slot_size;
  uint32_t actualSize;
  uint32_t element_size;
  int32_t native_size;
  uint32_t static_fields_size;
  uint32_t thread_static_fields_size;
  int32_t thread_static_fields_offset;
  uint32_t flags;
  uint32_t token;
  uint16_t method_count;
  uint16_t property_count;
  uint16_t field_count;
  uint16_t event_count;
  uint16_t nested_type_count;
  uint16_t vtable_count;
  uint16_t interfaces_count;
  uint16_t interface_offsets_count;
  uint8_t typeHierarchyDepth;
  uint8_t genericRecursionDepth;
  uint8_t rank;
  uint8_t minimumAlignment;
  uint8_t packingSize;
  uint8_t initialized_and_no_error : 1;
  uint8_t initialized : 1;
  uint8_t enumtype : 1;
  uint8_t nullabletype : 1;
  uint8_t is_generic : 1;
  uint8_t has_references : 1;
  uint8_t init_pending : 1;
  uint8_t size_init_pending : 1;
  uint8_t size_inited : 1;
  uint8_t has_finalize : 1;
  uint8_t has_cctor : 1;
  uint8_t is_blittable : 1;
  uint8_t is_import_or_windows_runtime : 1;
  uint8_t is_vtable_initialized : 1;
  uint8_t is_byref_like : 1;
  VirtualInvokeData vtable[1];
};

struct Il2CppObject {
  uintptr_t klass;   // Il2CppClass*
  uintptr_t monitor; // void*
};

struct Il2CppArrayBounds {
  uintptr_t length;
  int32_t lower_bound;
};

struct Il2CppArray {
  Il2CppObject obj;
  uintptr_t bounds; // Il2CppArrayBounds*
  uintptr_t max_length;
  uintptr_t items[1];
};

struct Il2CppString {
  Il2CppObject obj;
  int32_t length;
  uint16_t chars[1];
};

struct System_Collections_Generic_List {
  Il2CppObject obj;
  uintptr_t items; // Il2CppArray*
  int32_t size;
  int32_t version;
  uintptr_t syncRoot; // Il2CppObject*
};

} // namespace il2cpp