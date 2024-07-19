#if __linux__
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#elif _WIN32
#define _AMD64_
#include <fileapi.h>
#include <handleapi.h>
#include <memoryapi.h>
#endif

#include "mmap_view.h"

#if __linux__

static const auto PAGESIZE = sysconf(_SC_PAGESIZE);

bool MmapView::open(const std::filesystem::path& filePath)
{
  this->close();

  // Open the file
  const int fd = ::open(filePath.c_str(), O_RDONLY, 0);
  if (fd == -1)
    return false;

  // Determine the file size
  const size_t fileSize = ::lseek(fd, 0, SEEK_END);
  if (fileSize == -1 || (::lseek(fd, 0, SEEK_SET) != 0)) {
    ::close(fd);
    return false;
  }

  // Round it to a page boundary
  const size_t mappedSize = (fileSize + PAGESIZE - 1) & ~(PAGESIZE - 1u);

  // Perform the mmap, and close the file handle immediately after (as per the docs, we can do this).
  const void* data = ::mmap(nullptr, mappedSize, PROT_READ, MAP_POPULATE | MAP_SHARED, fd, 0);
  ::close(fd);
  if (data == MAP_FAILED)
    return false;

  // Save the state
  m_state.data = data;
  m_state.size = fileSize;
  m_state.mappedSize = mappedSize;

  return true;
}

void MmapView::close()
{
  if (!this->isOpen())
    return;

  ::munmap((void*)m_state.data, m_state.mappedSize);
  m_state = {};
}

#elif _WIN32

bool MmapView::open(const std::filesystem::path& filePath)
{
  this->close();

  // Open the file
  const HANDLE handleFile =
    ::CreateFileW(filePath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (handleFile == INVALID_HANDLE_VALUE) {
    return false;
  }

  // Determine the file size
  LARGE_INTEGER fileSize{};
  if (!::GetFileSizeEx(handleFile, &fileSize)) {
    ::CloseHandle(handleFile);
    return false;
  }

  // Map the file
  const HANDLE handleMapping = ::CreateFileMappingW(handleFile, NULL, PAGE_READONLY, 0, 0, NULL);
  if (!handleMapping) {
    ::CloseHandle(handleFile);
    return false;
  }

  const void* data = ::MapViewOfFile(handleMapping, FILE_MAP_READ | FILE_MAP_COPY, 0, 0, 0);
  if (!data) {
    ::CloseHandle(handleMapping);
    ::CloseHandle(handleFile);
    return false;
  }

  // Get the actual size of the mapped memory region
  MEMORY_BASIC_INFORMATION mbi;
  if (!::VirtualQuery(data, &mbi, sizeof(mbi))) {
    ::UnmapViewOfFile(data);
    ::CloseHandle(handleMapping);
    ::CloseHandle(handleFile);
    return false;
  }

  // Save the state
  m_state.data = data;
  m_state.size = (size_t)fileSize.QuadPart;
  m_state.mappedSize = (size_t)mbi.RegionSize;
  m_state.handleFile = handleFile;
  m_state.handleMapping = handleMapping;

  return true;
}

void MmapView::close()
{
  if (!this->isOpen())
    return;

  ::UnmapViewOfFile(m_state.data);
  ::CloseHandle(m_state.handleMapping);
  ::CloseHandle(m_state.handleFile);

  m_state = {};
}

#endif
