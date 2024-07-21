#if __linux__
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <cerrno>
#include <linux/limits.h>
#elif _WIN32
#define _AMD64_
#include <fileapi.h>
#include <handleapi.h>
#include <memoryapi.h>
#include <sysinfoapi.h>
#include <processthreadsapi.h>
#include <errhandlingapi.h>
#include <winerror.h>
#include <tlhelp32.h>
#include <psapi.h>
#endif

#include "rpm.h"

#if __linux__

PID WinRPM::getPIDByFilename(PathViewType processFilename)
{
  // Alternatively, without procfs: ::readproc()

  // Enumerate the processes
  for (const auto& proc : std::filesystem::directory_iterator("/proc")) {
    if (!proc.is_directory())
      continue;
    const auto& procPath = proc.path();
    PID pid = 0;
    {
      const auto PIDstr = procPath.filename();
      const auto* PIDcstr = PIDstr.c_str();

      // Fast fail
      if (!('0' < PIDcstr[0] && PIDcstr[0] <= '9'))
        continue;

      // Parse PID
      //  Entries in /proc/* that start with a number should be just a number, and they should represent a process.
      errno = 0;
      pid = ::strtoul(PIDcstr, NULL, 10);
      if (errno != 0)
        continue;
    }

    // Check the process name
    {
      std::error_code fec;
      const auto exePath = std::filesystem::read_symlink(procPath / "exe", fec);
      if (fec)
        continue;
      const auto exeFilename = exePath.filename();

      // Look for the wine preloader
      if (exeFilename != "wine64-preloader" && exeFilename != "wine-preloader")
        continue;
    }

    // Check the win exe name, which is contained in argv[0]
    {
      // Read argv[0]
      std::string argv0;
      if (!std::getline(std::ifstream(procPath / "cmdline"), argv0, '\0'))
        continue;

      // Find the filename contained in argv[0]
      const auto slashPos = argv0.find_last_of("\\/");
      if (argv0.compare(slashPos == argv0.npos ? 0 : slashPos + 1, argv0.npos, processFilename))
        continue;
    }

    // Try to open the handle
    return pid;
  }

  return 0;
}

WinRPM::OpenResult WinRPM::open(PID pid)
{
  // Close old
  this->close();

  // Open new
  char buffer[32];
  ::snprintf(buffer, sizeof(buffer), "/proc/%u/mem", pid);
  m_state.handle = ::open(buffer, O_RDWR | O_LARGEFILE);
  if (m_state.handle < 0) {
    if (errno == EACCES)
      return WinRPM::OpenResult::NoPrivileges;
    if (errno == ENOENT)
      return WinRPM::OpenResult::NotFound;
    return WinRPM::OpenResult::Error;
  }
  m_state.pid = pid;
  return WinRPM::OpenResult::Ok;
}

WinRPM::ModuleInfo WinRPM::getModuleInfo(PathViewType moduleName)
{
  // On Wine this is basically the same
  const auto info = this->getMappedFileInfo(moduleName);
  return {info.range.start, std::move(info.path)};
}

WinRPM::MappedFileInfo WinRPM::getMappedFileInfo(PathViewType filename)
{
  if (!this->isOpen())
    return {};

  char buffer[128 + PATH_MAX];
  ::sprintf(buffer, "/proc/%u/maps", m_state.pid);
  FILE* fpMaps = ::fopen(buffer, "r");

  if (!fpMaps)
    return {};

  // Now that we have opened *a* /proc/*/maps, check whether our mem handle is still valid. Because if it's not, then
  // that means that our process has been closed and its PID has been reassigned, and thus we should just exit out.
  if (!this->pollIsOpen()) {
    ::fclose(fpMaps);
    return {};
  }

  while (::fgets(buffer, sizeof(buffer), fpMaps)) {
    // Parse the map
    char perms[5];
    uintptr_t addrStart, addrEnd;
    unsigned long long fileOffset, inode;
    unsigned devMajor, devMinor;
    int pathPos;
    if (::sscanf(
          buffer, "%lx-%lx %4s %llx %x:%x %llu%n", &addrStart, &addrEnd, perms, &fileOffset, &devMajor, &devMinor,
          &inode, &pathPos
        ) < 7)
      continue;

    // We are looking for the base address
    if (fileOffset != 0)
      continue;

    // Swallow the whitespaces before the path
    const char* pathBegin = &buffer[pathPos];
    while (*pathBegin == ' ' || *pathBegin == '\t')
      pathBegin++;

    std::string_view path{pathBegin};

    // Swallow the new line from ::fgets
    if (!path.empty() && path[path.size() - 1] == '\n')
      path.remove_suffix(1);

    // Fast fail
    if (path.size() < filename.size())
      continue;

    // Check for a filename match (might be slightly faster than creating an std::filesystem::path every time)
    const auto slashPos = path.rfind(L'/');
    if (!path.compare(slashPos == path.npos ? 0 : slashPos + 1, path.npos, filename)) {
      ::fclose(fpMaps);
      return {{addrStart, addrEnd}, path};
    }
  }

  ::fclose(fpMaps);
  return {};
}

void WinRPM::close()
{
  if (!this->isOpen())
    return;
  ::close(m_state.handle);
  m_state = {};
}

bool WinRPM::pollIsOpen()
{
  if (!this->isOpen())
    return false;

  // Explanation:
  //  Reading non-zero amount of bytes from /proc/*/mem will either return -1 (+ set errno), the number of bytes read
  //  (a non-zero number) or 0. The 0 is due to an early exit if the process got closed.
  // I'm not aware of a nicer way of checking whether the process is still running.
  char dummy;
  if (::pread(m_state.handle, &dummy, sizeof(dummy), 0) == 0) {
    this->close();
    return false;
  }
  return true;
}

bool WinRPM::read_raw(uintptr_t remoteAddr, void* dataOut, size_t dataSize)
{
  if (!this->isOpen())
    return false;

  // If dataSize == 0, we should still check whether the handle is still valid.
  if (!dataSize)
    return this->pollIsOpen();

  ssize_t bytes{};
  if ((bytes = ::pread(m_state.handle, dataOut, dataSize, remoteAddr)) == -1)
    return false;

  // See: the comment in pollIsOpen
  if (!bytes) {
    this->close();
    return false;
  }
  return bytes == dataSize;
}

bool WinRPM::write_raw(uintptr_t remoteAddr, const void* dataIn, size_t dataSize)
{
  if (!this->isOpen())
    return false;

  // If dataSize == 0, we should still check whether the handle is still valid.
  if (!dataSize)
    return this->pollIsOpen();

  ssize_t bytes{};
  if ((bytes = ::pwrite(m_state.handle, dataIn, dataSize, remoteAddr)) == -1)
    return false;

  // See: the comment in pollIsOpen
  if (!bytes) {
    this->close();
    return false;
  }
  return bytes == dataSize;
}

#elif _WIN32

PID WinRPM::getPIDByFilename(PathViewType processFilename)
{
  // Create snapshot
  HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (!snapshot)
    return 0;

  // Enumerate processes
  PROCESSENTRY32W pe;
  pe.dwSize = sizeof(pe);
  for (bool hasEntry = ::Process32FirstW(snapshot, &pe); hasEntry; hasEntry = ::Process32NextW(snapshot, &pe)) {
    if (processFilename.compare(pe.szExeFile) == 0) {
      ::CloseHandle(snapshot);
      return pe.th32ProcessID;
    }
  }

  ::CloseHandle(snapshot);
  return 0;
}

WinRPM::OpenResult WinRPM::open(PID pid)
{
  // Close old
  this->close();

  // Open new
  m_state.handle = ::OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, false, pid);
  if (!m_state.handle)
    return ::GetLastError() == ERROR_ACCESS_DENIED ? WinRPM::OpenResult::NoPrivileges : WinRPM::OpenResult::Error;
  m_state.pid = pid;
  return WinRPM::OpenResult::Ok;
}

WinRPM::ModuleInfo WinRPM::getModuleInfo(PathViewType moduleName)
{
  if (!this->isOpen())
    return {};

  // Create snapshot
  HANDLE snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, m_state.pid);
  if (!snapshot) {
    this->pollIsOpen();
    return {};
  }

  // Enumerate processes
  MODULEENTRY32W me;
  me.dwSize = sizeof(me);
  for (bool hasEntry = ::Module32FirstW(snapshot, &me); hasEntry; hasEntry = ::Module32NextW(snapshot, &me)) {
    if (moduleName.compare(me.szModule) == 0) {
      ::CloseHandle(snapshot);
      return {(uintptr_t)me.modBaseAddr, me.szExePath};
    }
  }

  ::CloseHandle(snapshot);
  return {};
}

static const SYSTEM_INFO g_systemInfo = []() {
  SYSTEM_INFO si;
  ::GetSystemInfo(&si);
  return si;
}();

static std::filesystem::path devicePathToDosPath(std::wstring_view path)
{
  // Wine can incorrectly returns an NT path  ("\??\...") insted of a device path ("\Device\HarddiskVolume1\...").
  // The tests for this in wine are also marked as todo.
  if (path.starts_with(L"\\??\\"))
    path.remove_prefix(4);

  // Fast fail if either the path is too short or already contains a volume / drive letter.
  if (path.size() < 3 || (path[1] == L':' && path[2] == L'\\'))
    return path;

  // Assume that it's a device path
  // NOTE: On native Windows, prefixing the path with "\\?\GLOBALROOT" would also be an option if we only want to
  //  use it in winapi functions (and not with std::filesystem).
  wchar_t drive[3] = L"A:";
  wchar_t dosDeviceBuffer[MAX_PATH];
  for (DWORD drives = ::GetLogicalDrives(); drives; ++drive[0], drives >>= 1u) {
    // Is this a drive available?
    if (!(drives & 1))
      continue;

    // NOTE: Docs say QueryDosDeviceW might return multiple strings
    if (!::QueryDosDeviceW(drive, dosDeviceBuffer, (DWORD)std::size(dosDeviceBuffer)))
      continue;

    std::wstring_view dosDevice{dosDeviceBuffer};
    if (path.starts_with(dosDevice)) {
      std::wstring dosPath;
      dosPath.reserve(2 + path.size() - dosDevice.size());
      dosPath.assign(drive, 2);
      dosPath.append(path.substr(dosDevice.size()));
      return std::move(dosPath);
    }
  };

  return path;
}

WinRPM::MappedFileInfo WinRPM::getMappedFileInfo(PathViewType filename)
{
  if (!this->isOpen())
    return {};

  MEMORY_BASIC_INFORMATION mbi;
  for (LPVOID address = g_systemInfo.lpMinimumApplicationAddress; address < g_systemInfo.lpMaximumApplicationAddress;
       address = (LPVOID)((uintptr_t)address + mbi.RegionSize)) {
    // Query info about the memory region
    if (!::VirtualQueryEx(m_state.handle, address, &mbi, sizeof(mbi))) {
      this->pollIsOpen();
      break;
    }

    // Look for mapped regions backed by a file
    if (mbi.Type == MEM_MAPPED && mbi.State == MEM_COMMIT) {
      wchar_t pathBuffer[MAX_PATH];
      const auto pathLength = ::GetMappedFileNameW(m_state.handle, address, pathBuffer, MAX_PATH);
      if (!pathLength)
        continue;

      std::wstring_view path{pathBuffer, pathLength};

      // Fast fail (because device paths can only shrink)
      if (path.size() < filename.size())
        continue;

      // Check for a filename match (might be slightly faster than creating an std::filesystem::path every time)
      const auto slashPos = path.rfind(L'\\');
      if (!path.compare(slashPos == path.npos ? 0 : slashPos + 1, path.npos, filename)) {
        return {
          {(uintptr_t)mbi.BaseAddress, (uintptr_t)mbi.BaseAddress + (uintptr_t)mbi.RegionSize},
          devicePathToDosPath(path)
        };
      }
    }
  }

  return {};
}

void WinRPM::close()
{
  if (!this->isOpen())
    return;
  ::CloseHandle(m_state.handle);
  m_state = {};
}

bool WinRPM::pollIsOpen()
{
  if (!this->isOpen())
    return false;
  DWORD exitCode = 0;
  if (!::GetExitCodeProcess(m_state.handle, &exitCode) || exitCode != STILL_ACTIVE)
    this->close();
  return this->isOpen();
}

bool WinRPM::read_raw(uintptr_t remoteAddr, void* dataOut, size_t dataSize)
{
  if (!this->isOpen())
    return false;
  if (!::ReadProcessMemory(m_state.handle, (LPCVOID)remoteAddr, dataOut, dataSize, NULL)) {
    // Almost everything will throw an ERROR_PARTIAL_COPY
    this->pollIsOpen();
    return false;
  }
  return true;
}

bool WinRPM::write_raw(uintptr_t remoteAddr, const void* dataIn, size_t dataSize)
{
  if (!this->isOpen())
    return false;
  if (!::WriteProcessMemory(m_state.handle, (LPVOID)remoteAddr, dataIn, dataSize, NULL)) {
    // Almost everything will throw an ERROR_PARTIAL_COPY
    this->pollIsOpen();
    return false;
  }
  return true;
}

#endif