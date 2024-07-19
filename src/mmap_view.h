#pragma once

#include <cstdint>
#include <utility>
#include <filesystem>
#include <span>

// Forward declare some stuff
#if __linux__
#elif _WIN32
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(uintptr_t)(-1))
#endif
typedef void* HANDLE;
#endif

class MmapView
{
protected:
  struct State {
    const void* data = nullptr; // Pointer to the mapped data in memory
    size_t size = 0;            // The (requested) size of the mapped data
    size_t mappedSize = 0;      // The (actual) size of the mapped data (basically m_size rounded up to a page boundary)

#if __linux__
#elif _WIN32
    HANDLE handleMapping = 0;
    HANDLE handleFile = INVALID_HANDLE_VALUE;
#endif
  } m_state;

public:
  MmapView() noexcept = default;
  MmapView(const std::filesystem::path& filePath) { this->open(filePath); };
  ~MmapView() { this->close(); }

  MmapView(const MmapView&) = delete;
  MmapView& operator=(const MmapView&) = delete;

  MmapView(MmapView&& rhs) noexcept : m_state(std::exchange(rhs.m_state, {})) {};
  MmapView& operator=(MmapView&& rhs)
  {
    this->close();
    m_state = std::exchange(rhs.m_state, {});
    return *this;
  };

  /**
   * Tries to map a file into memory for reading.
   * Return indicates success. Previously mapped files will be automatically closed.
   */
  bool open(const std::filesystem::path& filePath);

  /**
   * Unmaps the file from memory.
   */
  void close();
  inline constexpr bool isOpen() const { return m_state.data != nullptr; }
  explicit inline constexpr operator bool() const { return isOpen(); }

  inline const unsigned char* data() const { return static_cast<const unsigned char*>(m_state.data); }
  inline const unsigned char& operator[](size_t offset) const { return *(this->data() + offset); }

  template <typename T>
  inline auto getPtr(size_t offset) const
    requires(std::is_trivial_v<std::decay_t<T>>)
  {
    return (const std::decay_t<T>*)(this->data() + offset);
  }

  template <typename T>
  inline auto& get(size_t offset) const
    requires(std::is_trivial_v<std::decay_t<T>>)
  {
    return *(const std::decay_t<T>*)(this->data() + offset);
  }

  inline size_t size() const { return m_state.size; }
  inline size_t mappedSize() const { return m_state.mappedSize; }
};