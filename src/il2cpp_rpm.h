#pragma once

#include <filesystem>
#include <span>
#include <functional>
#include <optional>

#include "rpm.h"
#include "mmap_view.h"
#include "il2cpp_structs.h"

struct Il2CppId {
  std::string_view name;
  std::string_view namespaze;

  inline constexpr bool equal(std::string_view name_, std::string_view namespaze_) const
  {
    return name == name_ && namespaze == namespaze_;
  }
  inline constexpr bool operator==(const Il2CppId&) const = default;
  inline constexpr operator bool() const { return !name.empty(); };
};

/**
 * A simplpe class to read and write the memory of Il2Cpp Unity games remotely.
 */
class Il2CppRPM
{
protected:
  WinRPM m_rpm;
  uintptr_t m_gameAssemblyBase{};
  MemRange m_metadataRange{};
  MmapView m_metadataView;

  bool m_verbose = false;

public:
  Il2CppRPM() = default;
  Il2CppRPM(WinRPM::PathViewType processName) { this->open(processName); };
  ~Il2CppRPM() { this->close(); }

  enum class OpenResult {
    Ok,
    NotFound,
    NoPrivileges,
    WinRPMError,
    Il2CppError
  };

  /**
   * Tries to open a remote il2cpp process.
   */
  OpenResult open(WinRPM::PathViewType processName);

  /**
   * Closes the handle to the process and resets the internal state.
   */
  void close();

  inline bool isOpen() const { return m_rpm.isOpen(); }
  explicit inline operator bool() const { return isOpen(); }

  inline bool isVerbose() const { return m_verbose; }
  inline void setVerbose(bool verbose) { m_verbose = verbose; }

  // -------------------------------------------------------------------

  /**
   * Fast check for valid pointers.
   */
  inline static bool isValidRemotePtr(uintptr_t remotePtr) { return (0 < remotePtr && remotePtr < (1ull << 48)); }

  /**
   * Returns a reference to the metadata header.
   */
  inline const il2cpp::Il2CppGlobalMetadataHeader& meta_getHeader() const
  {
    return m_metadataView.get<const il2cpp::Il2CppGlobalMetadataHeader>(0);
  }

  /**
   * Maps a remote pointer inside the remote global-metadata.dat to a local pointer inside our own mapped version of
   * global-metadata.dat .
   * Returns a null pointer upon error.
   */
  template <typename T>
  inline auto meta_ptrToLocal(uintptr_t remotePtr) const
  {
    if (!m_metadataRange.in(remotePtr))
      return (const T*)nullptr;
    return m_metadataView.getPtr<T>(remotePtr - m_metadataRange.start);
  }

  /**
   * Gets something from one of the tables defined in the metadata header.
   * The offset and size mean the [TABLE_NAME]Offset and [TABLE_NAME]Size fields in the metadata header respectively.
   */
  template <typename T>
  inline auto meta_getLocalByIdx(uintptr_t offset, uintptr_t size, uintptr_t index) const
  {
    if (index >= size)
      return (const T*)nullptr;
    return m_metadataView.getPtr<T>(offset + index);
  }

  /**
   * Gets a string from the metadata's string table.
   */
  std::optional<std::string_view> meta_getStrByIdx(uintptr_t index, size_t maxLen = 512) const;

  /**
   * Maps a remote string pointer inside the remote global-metadata.dat to a local pointer inside our own mapped
   * version of global-metadata.dat .
   */
  std::optional<std::string_view> meta_remoteStrToLocal(uintptr_t remotePtr, size_t maxLen = 512) const;

  /**
   * Heuristically checks whether the remote pointer points to an il2cpp class instance, and if so, then returns its
   * class name and namespace.
   */
  bool il2cpp_class_heuristicCheck(uintptr_t classPtr, Il2CppId& classId);

  /**
   * Retrieves the class instance of an Il2CppObject object.
   * It returns 0 upon error.
   */
  uintptr_t il2cpp_obj_getClassInstance(uintptr_t objPtr);

  /**
   * Retrieves the name of an Il2CppClass instance.
   */
  std::optional<std::string_view> il2cpp_class_getName(uintptr_t classPtr);

  /**
   * Retrieves the namespace of an Il2CppClass instance.
   */
  std::optional<std::string_view> il2cpp_class_getNamespace(uintptr_t classPtr);

  /**
   * Checks if an Il2CppClass has the given name and namespace.
   */
  bool il2cpp_class_hasNameAndNamespace(uintptr_t classPtr, const Il2CppId& id);

  /**
   * Retrieves the name of an Il2CppTypeDefinition instance.
   */
  std::optional<std::string_view> il2cpp_typedef_getName(uintptr_t typedefPtr);

  /**
   * Retrieves the namespace of an Il2CppTypeDefinition instance.
   */
  std::optional<std::string_view> il2cpp_typedef_getNamespace(uintptr_t typedefPtr);

  /**
   * Checks if an Il2CppTypeDefinition has the given name and namespace.
   */
  bool il2cpp_typedef_hasNameAndNamespace(uintptr_t typedefPtr, const Il2CppId& id);

  /**
   * Enumerates a class' fields while the callback returns true.
   */
  void il2cpp_class_enumFields(
    uintptr_t classPtr, std::function<bool(const il2cpp::FieldInfo& field)> callback, uint16_t maxFields = 512u
  );

  /**
   * Reads an Il2CppString in its original UTF-16 form.
   */
  bool il2cpp_string_readUTF16(uintptr_t strPtr, std::u16string& out);

  /**
   * Reads an Il2CppString as UTF-16 and converts it to UTF-8.
   * NOTE: It internally uses std::wstring_convert , so this function is best avoided.
   */
  bool il2cpp_string_readUTF8(uintptr_t strPtr, std::string& out);

  /**
   * Reads a System.Collections.Generic.List<T> and returns the actual size of the underlying list, while optionally
   * reading the contents of the list up to maxCount if an output vector is specified.
   * Upon error, it returns -1.
   */
  size_t il2cpp_genericList_read(uintptr_t listPtr, std::vector<uintptr_t>* out = nullptr, size_t maxCount = -1);
};