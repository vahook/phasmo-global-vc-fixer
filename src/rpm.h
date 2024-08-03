#pragma once

#include <cinttypes>
#include <filesystem>
#include <utility>

// Forward declare some stuff
#if __linux__
#include <ctime>
using PID = int; // pid_t
#elif _WIN32
typedef void* HANDLE;
using PID = uint32_t; // DWORD
#endif

struct MemRange {
  uintptr_t start = 0;
  uintptr_t end = 0;

  inline constexpr uintptr_t size() const { return end - start; }
  inline constexpr bool empty() const { return this->size() == 0; }
  explicit constexpr inline operator bool() const { return !this->empty(); }

  inline constexpr bool in(uintptr_t addr) const { return start <= addr && addr < end; }
};

/**
 * A simple class to read a remote process' memory. It works on both native Windows and Wine.
 * It doesn't support Windows long paths.
 */
class WinRPM
{
protected:
  struct State {
    PID pid = 0;
#if __linux__
    int handle = -1;
#elif _WIN32
    HANDLE handle = 0;
#endif
  } m_state;

public:
  /**
   * Note (rant):
   *  File paths are a complete mess on Windows and C++ (as of C++20) lacks null-terminated string views, so I suppose
   *  this is the best option when considering both performance and lines-of-code added to handle special Microsoft
   *  bullshit.
   */
  using PathViewType = std::basic_string_view<std::filesystem::path::value_type>;

  // Similar to TEXT(), but cross-platform, and only for paths
#if __linux__
#define WINRPM_PATH(PATH) PATH
#elif _WIN32
#define WINRPM_PATH(PATH) L##PATH
#endif

  WinRPM() noexcept = default;
  WinRPM(PID pid) { this->open(pid); }
  WinRPM(PathViewType processFilename) { this->open(processFilename); }
  ~WinRPM() { this->close(); }

  WinRPM(const WinRPM&) = delete;
  WinRPM& operator=(const WinRPM&) = delete;

  WinRPM(WinRPM&& rhs) noexcept : m_state(std::exchange(rhs.m_state, {})) {}
  WinRPM& operator=(WinRPM&& rhs)
  {
    this->close();
    m_state = std::exchange(rhs.m_state, {});
    return *this;
  }

  enum class OpenResult {
    Ok,
    NotFound,
    NoPrivileges,
    Error
  };

  /**
   * Returns the PID of the first running process with a given filename.
   * If no such process can be found, 0 will be returned.
   */
  static PID getPIDByFilename(PathViewType processFilename);

  /**
   * Opens a remote process based on its PID.
   * Previously opened processes will be automatically closed.
   */
  OpenResult open(PID pid);

  /**
   * Opens a remote process based on its executable's name.
   * Previously opened processes will be automatically closed.
   */
  OpenResult open(PathViewType processFilename)
  {
    // Close old
    this->close();
    if (PID pid = WinRPM::getPIDByFilename(processFilename))
      return this->open(pid);
    return WinRPM::OpenResult::NotFound;
  }

  struct ModuleInfo {
    uintptr_t base{};
    std::filesystem::path path;
  };

  /**
   * Finds the first loaded module in the remote process with a given name, and returns info about it.
   * Upon failure, the base address will be set to 0.
   */
  ModuleInfo getModuleInfo(PathViewType moduleName);

  struct MappedFileInfo {
    MemRange range{};
    std::filesystem::path path;
  };

  /**
   * Finds the first memory region that is backed by a file with a given filename, and returns info about it.
   * Upon failure, the region's base address will be set to 0.
   */
  MappedFileInfo getMappedFileInfo(PathViewType filename);

  /**
   * Closes the handle to the process.
   */
  void close();

  /**
   * Returns whether there is a process currently open.
   */
  inline bool isOpen() const
  {
#if __linux__
    return m_state.handle > -1;
#elif _WIN32
    return m_state.handle != 0;
#endif
  }
  explicit inline operator bool() const { return isOpen(); }

  inline PID getPID() const { return m_state.pid; }

  /**
   * Polls the OS to see whether the handle is still valid.
   */
  bool pollIsOpen();

  /**
   * Reads the remote process's memory.
   */
  bool read_raw(uintptr_t remoteAddr, void* dataOut, size_t dataSize);

  /**
   * Writes the remote process's memory.
   */
  bool write_raw(uintptr_t remoteAddr, const void* dataIn, size_t dataSize);

  /**
   * Reads an object of type T from the remote process' memory.
   */
  template <typename T>
  inline bool read(uintptr_t remoteAddr, T& dataOut, size_t offset = 0)
    requires(std::is_trivially_copyable_v<T>)
  {
    return this->read_raw(remoteAddr + offset, &dataOut, sizeof(T));
  }

  /**
   * Reads an object of type T from the remote process' memory through a pointer chain.
   * That is: *(*( ... *(*(remoteAddr + offset_1) + offset_2) ... ) + offset_n)
   */
  template <typename T, typename... OFFSETS>
  inline bool read(uintptr_t remoteAddr, T& dataOut, size_t offset_1, size_t offset_2, OFFSETS... offset_n)
    requires(std::is_trivially_copyable_v<T>)
  {
    // NOTE: sadly C++ as of C++20 doesn't have homogeneous function parameter packs :(
    if (!this->read<uintptr_t>(remoteAddr, remoteAddr, offset_1))
      return false;
    return this->read<T>(remoteAddr, dataOut, offset_2, offset_n...);
  }

  /**
   * Writes an object of type T the the remote process' memory.
   */
  template <typename T>
  inline bool write(uintptr_t remoteAddr, const T& dataIn, size_t offset = 0)
    requires(std::is_trivially_copyable_v<T>)
  {
    return this->write_raw(remoteAddr + offset, &dataIn, sizeof(T));
  }

  /**
   * Writes an object of type T to the remote process' memory through a pointer chain.
   * That is: *(*( ... *(*(remoteAddr + offset_1) + offset_2) ... ) + offset_n) = dataIn
   */
  template <typename T, typename... OFFSETS>
  inline bool write(uintptr_t remoteAddr, const T& dataIn, size_t offset_1, size_t offset_2, OFFSETS... offset_n)
    requires(std::is_trivially_copyable_v<T>)
  {
    if (!this->read<uintptr_t>(remoteAddr, remoteAddr, offset_1))
      return false;
    return this->write<T>(remoteAddr, dataIn, offset_2, offset_n...);
  }
};