// Sadly, C++20 still lacks a proper UTF implementation
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING

#include <array>
#include <thread>
#include <iostream>
#include <fstream>
#include <format>
#include <codecvt>
#include <cstring>

#include "il2cpp_rpm.h"
#include "rpm.h"

// clang-format off
#define LOG_COUT(...) std::cout << __VA_ARGS__
#define LOG_COUTF(...) std::cout << std::format(__VA_ARGS__)
#define LOG_CERR(...) std::cerr << __VA_ARGS__
#define LOG_CERRF(...) std::cerr << std::format(__VA_ARGS__)
#define LOG_VERB(...) if (m_verbose) std::cerr << __VA_ARGS__
#define LOG_VERBF(...) if (m_verbose) std::cerr << std::format(__VA_ARGS__)
// clang-format on

Il2CppRPM::OpenResult Il2CppRPM::open(WinRPM::PathViewType processName)
{
  if (const auto openResult = m_rpm.open(processName); openResult != WinRPM::OpenResult::Ok)
    return (Il2CppRPM::OpenResult)openResult;

  // Get the base address of GameAssembly.dll and global-metadata.dat
  m_gameAssemblyBase = m_rpm.getModuleInfo(WINRPM_PATH("GameAssembly.dll")).base;
  if (!m_gameAssemblyBase) {
    LOG_VERB("[Error]: Couldn't find the base address of 'GameAssembly.dll'.\n");
    this->close();
    return Il2CppRPM::OpenResult::Il2CppError;
  }

  const auto globalMetadata = m_rpm.getMappedFileInfo(WINRPM_PATH("global-metadata.dat"));
  if (!(m_metadataRange = globalMetadata.range)) {
    LOG_VERB("[Error]: Couldn't find the address of 'global-metadata.dat'.\n");
    this->close();
    return Il2CppRPM::OpenResult::Il2CppError;
  }

  if (!m_metadataView.open(globalMetadata.path)) {
    LOG_VERB("[Error]: Couldn't map 'global-metadata.dat' into memory.\n");
    this->close();
    return Il2CppRPM::OpenResult::Il2CppError;
  }

  if (m_metadataView.mappedSize() != m_metadataRange.size()) {
    LOG_VERBF(
      "[Error]: Memory mapped sizes differ: [local: {:#016x}, remote: {:#016x}]\n", m_metadataView.mappedSize(),
      m_metadataRange.size()
    );
    this->close();
    return Il2CppRPM::OpenResult::Il2CppError;
  }

  // Validate header
  const auto& metaHeader = this->meta_getHeader();

  // Check magic
  if (metaHeader.sanity != 0xFAB11BAF) {
    LOG_VERBF("[Error]: Invalid magic. [Expected: 0xFAB11BAF, got: {:#016}]\n", metaHeader.sanity);
    this->close();
    return Il2CppRPM::OpenResult::Il2CppError;
  }

  // Check version
  if (metaHeader.version < 29) {
    LOG_VERBF("[Error]: Expected version >= 29. [got: {}]\n", metaHeader.version);
    this->close();
    return Il2CppRPM::OpenResult::Il2CppError;
  }

  LOG_CERRF("[Info]: Opened il2cpp process [PID: {}].\n", m_rpm.getPID());
  LOG_VERBF(
    "[Debug]: [il2cpp version: {}, GameAssembly.exe base: {:#016x}, global-metadata.dat addr: {:#016x}-{:#016x}]\n",
    metaHeader.version, m_gameAssemblyBase, m_metadataRange.start, m_metadataRange.end
  );

  return Il2CppRPM::OpenResult::Ok;
}

void Il2CppRPM::close()
{
  m_rpm.close();
  m_metadataView.close();
  m_gameAssemblyBase = {};
  m_metadataRange = {};
}

inline static size_t strnlen_s_impl(const char* str, size_t strsz)
{
#if __STDC_LIB_EXT1__
  return ::strnlen_s(str, strsz);
#else
  const auto nullpos = (const char*)::memchr(str, '\0', strsz);
  return nullpos ? ((size_t)nullpos - (size_t)str) : strsz;
#endif
}

/**
 * Gets a string from the metadata's string table.
 */
std::optional<std::string_view> Il2CppRPM::meta_getStrByIdx(uintptr_t index, size_t maxLen) const
{
  const auto& header = this->meta_getHeader();
  if (const auto strPtr = this->meta_getLocalByIdx<const char>(header.stringOffset, header.stringSize, index))
    return std::string_view{strPtr, strnlen_s_impl(strPtr, maxLen)};
  return {};
}

/**
 * Maps a remote string pointer inside the remote global-metadata.dat to a local pointer inside our own mapped
 * version of global-metadata.dat .
 */
std::optional<std::string_view> Il2CppRPM::meta_remoteStrToLocal(uintptr_t remotePtr, size_t maxLen) const
{
  if (const auto strPtr = this->meta_ptrToLocal<const char>(remotePtr))
    return std::string_view{strPtr, strnlen_s_impl(strPtr, maxLen)};
  return {};
}

bool Il2CppRPM::il2cpp_class_heuristicCheck(uintptr_t classPtr, Il2CppId& classId)
{
  // Validate the remote pointer
  if (!Il2CppRPM::isValidRemotePtr(classPtr))
    return false;

  // Try and read the limited class header
  struct {
    uintptr_t image;     // void*
    uintptr_t gc_desc;   // void*
    uintptr_t name;      // const char*
    uintptr_t namespaze; // const char*
    il2cpp::Il2CppType byval_arg;
    il2cpp::Il2CppType this_arg;
  } classInst;
  if (!m_rpm.read(classPtr, classInst))
    return false;

  // Look for classes
  if (classInst.byval_arg.type != il2cpp::Il2CppTypeEnum::IL2CPP_TYPE_CLASS ||
      classInst.this_arg.type != il2cpp::Il2CppTypeEnum::IL2CPP_TYPE_CLASS)
    return false;

  const auto nameView = this->meta_remoteStrToLocal(classInst.name);
  if (!nameView)
    return false;
  const auto namespaceView = this->meta_remoteStrToLocal(classInst.namespaze);
  if (!namespaceView)
    return false;
  classId.name = *nameView;
  classId.namespaze = *namespaceView;
  return true;
}

uintptr_t Il2CppRPM::il2cpp_obj_getClassInstance(uintptr_t objPtr)
{
  uintptr_t classPtr;
  if (!m_rpm.read(objPtr, classPtr, offsetof(il2cpp::Il2CppObject, klass)))
    return {};
  return classPtr;
}

std::optional<std::string_view> Il2CppRPM::il2cpp_class_getName(uintptr_t classPtr)
{
  uintptr_t ptr;
  if (!m_rpm.read(classPtr, ptr, offsetof(il2cpp::Il2CppClass, name)))
    return {};
  return this->meta_remoteStrToLocal(ptr, 512); // 512 is a hard limit in C# for identifiers.
}

std::optional<std::string_view> Il2CppRPM::il2cpp_class_getNamespace(uintptr_t classPtr)
{
  uintptr_t ptr;
  if (!m_rpm.read(classPtr, ptr, offsetof(il2cpp::Il2CppClass, namespaze)))
    return {};
  return this->meta_remoteStrToLocal(ptr, 512); // 512 is a hard limit in C# for identifiers.
}

bool Il2CppRPM::il2cpp_class_hasNameAndNamespace(uintptr_t classPtr, const Il2CppId& id)
{
  struct {
    uintptr_t name;      // const char*
    uintptr_t namespaze; // const char*
  } classInst{};
  if (!m_rpm.read(classPtr, classInst, offsetof(il2cpp::Il2CppClass, name)))
    return false;
  return this->meta_remoteStrToLocal(classInst.name, 512) == id.name &&
         this->meta_remoteStrToLocal(classInst.namespaze, 512) == id.namespaze;
}

std::optional<std::string_view> Il2CppRPM::il2cpp_typedef_getName(uintptr_t typedefPtr)
{
  uint32_t nameIndex;
  if (!m_rpm.read(typedefPtr, nameIndex, offsetof(il2cpp::Il2CppTypeDefinition, nameIndex)))
    return {};
  return this->meta_getStrByIdx(nameIndex);
}

std::optional<std::string_view> Il2CppRPM::il2cpp_typedef_getNamespace(uintptr_t typedefPtr)
{
  uint32_t namespaceIndex;
  if (!m_rpm.read(typedefPtr, namespaceIndex, offsetof(il2cpp::Il2CppTypeDefinition, namespaceIndex)))
    return {};
  return this->meta_getStrByIdx(namespaceIndex);
}

bool Il2CppRPM::il2cpp_typedef_hasNameAndNamespace(uintptr_t typedefPtr, const Il2CppId& id)
{
  struct {
    uint32_t nameIndex;
    uint32_t namespaceIndex;
  } typedefInst;
  if (!m_rpm.read(typedefPtr, typedefInst, offsetof(il2cpp::Il2CppTypeDefinition, nameIndex)))
    return false;

  return this->meta_getStrByIdx(typedefInst.nameIndex) == id.name &&
         this->meta_getStrByIdx(typedefInst.namespaceIndex) == id.namespaze;
}

void Il2CppRPM::il2cpp_class_enumFields(
  uintptr_t classPtr, std::function<bool(const il2cpp::FieldInfo& field)> callback, uint16_t maxFields
)
{
  // Read the field count
  decltype(il2cpp::Il2CppClass::field_count) fieldCount{};
  if (!m_rpm.read(classPtr, fieldCount, offsetof(il2cpp::Il2CppClass, field_count)))
    return;
  fieldCount = std::min<decltype(fieldCount)>(fieldCount, maxFields);

  // Read the field array pointer
  uintptr_t filedsPtr;
  if (!m_rpm.read(classPtr, filedsPtr, offsetof(il2cpp::Il2CppClass, fields)))
    return;

  // Read the field array
  auto filedsArr = std::make_unique<il2cpp::FieldInfo[]>(fieldCount);
  if (!m_rpm.read_raw(filedsPtr, filedsArr.get(), sizeof(il2cpp::FieldInfo) * fieldCount))
    return;

  for (int i = 0; i < fieldCount; ++i) {
    const auto& field = filedsArr[i];
    if (!callback(field))
      break;
  }

  return;
}

bool Il2CppRPM::il2cpp_string_readUTF16(uintptr_t strPtr, std::u16string& out)
{
  il2cpp::Il2CppString str;
  if (!m_rpm.read(strPtr, str))
    return false;
  out.resize(str.length);
  return m_rpm.read_raw(strPtr + offsetof(il2cpp::Il2CppString, chars), out.data(), str.length * sizeof(str.chars[0]));
}

bool Il2CppRPM::il2cpp_string_readUTF8(uintptr_t strPtr, std::string& out)
{
  std::u16string tmp;
  if (!this->il2cpp_string_readUTF16(strPtr, tmp))
    return false;
  try {
    out = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.to_bytes(tmp);
  } catch (const std::range_error& e) {
    return false;
  }
  return true;
}

size_t Il2CppRPM::il2cpp_genericList_read(uintptr_t listPtr, std::vector<uintptr_t>* out, size_t maxCount)
{
  il2cpp::System_Collections_Generic_List list;
  if (!m_rpm.read<il2cpp::System_Collections_Generic_List>(listPtr, list))
    return -1;
  if (out) {
    const size_t size = std::min<size_t>(list.size, maxCount);
    out->resize(size);
    if (size)
      m_rpm.read_raw(list.items + offsetof(il2cpp::Il2CppArray, items), out->data(), size * sizeof(uintptr_t));
  }
  return list.size;
}