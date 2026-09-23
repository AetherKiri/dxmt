#include "wsi_platform.hpp"
#include <windows.h>
#include <cstdint>

namespace dxmt::wsi {

void *aligned_malloc(size_t size, size_t alignment) {
  /*
   * CPU-visible DXMT allocations are handed back through a D3D Map call.
   * The CRT heap is a native host allocation in the standalone Madeira
   * process, so an interpreted x86 guest cannot dereference it: TCTI only
   * knows about pages allocated through Wine's virtual-memory layer.  All
   * callers currently request the DXMT page size (4096 bytes); use the same
   * page allocator as Win32 applications so the returned address is part of
   * the guest address space and receives the normal Madeira memory events.
   */
  const size_t page_size = 4096;
  size_t rounded;

  if (!size) size = 1;
  if (alignment > page_size || (alignment & (alignment - 1)) != 0)
    return nullptr;
  if (size > SIZE_MAX - (page_size - 1)) return nullptr;
  rounded = (size + page_size - 1) & ~(page_size - 1);
  return VirtualAlloc(nullptr, rounded, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

void aligned_free(void *ptr) {
  if (ptr) VirtualFree(ptr, 0, MEM_RELEASE);
}

} // namespace dxmt::wsi
