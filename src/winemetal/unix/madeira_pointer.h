/* Guest pointer bridge used by the standalone Madeira-SE WoW64 path. */

#ifndef DXMT_MADEIRA_POINTER_H
#define DXMT_MADEIRA_POINTER_H

#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef void *(*madeira_guest_pointer_fn)(uint64_t);
typedef uint64_t (*madeira_host_pointer_fn)(const void *);

static inline madeira_guest_pointer_fn madeira_guest_pointer_resolver(void)
{
    static madeira_guest_pointer_fn function;
    static int resolved;
    void *symbol;
    void *library;
    const char *path;

    if (resolved) return function;
    resolved = 1;
    symbol = dlsym(RTLD_DEFAULT, "madeira_se_guest_pointer");
    path = getenv("MADEIRA_SE_HOST_NTDLL");
    if (!symbol && path && path[0]) {
        library = dlopen(path, RTLD_NOLOAD | RTLD_NOW | RTLD_GLOBAL);
        if (!library) library = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
        if (library) symbol = dlsym(library, "madeira_se_guest_pointer");
    }
    memcpy(&function, &symbol, sizeof(function));
    return function;
}

static inline madeira_host_pointer_fn madeira_host_pointer_resolver(void)
{
    static madeira_host_pointer_fn function;
    static int resolved;
    void *symbol;
    void *library;
    const char *path;

    if (resolved) return function;
    resolved = 1;
    symbol = dlsym(RTLD_DEFAULT, "madeira_se_host_pointer");
    path = getenv("MADEIRA_SE_HOST_NTDLL");
    if (!symbol && path && path[0]) {
        library = dlopen(path, RTLD_NOLOAD | RTLD_NOW | RTLD_GLOBAL);
        if (!library) library = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
        if (library) symbol = dlsym(library, "madeira_se_host_pointer");
    }
    memcpy(&function, &symbol, sizeof(function));
    return function;
}

static inline void *madeira_guest_to_host_pointer(const void *value)
{
    madeira_guest_pointer_fn function;
    uint64_t raw = (uint64_t)(uintptr_t)value;

    if (!raw) return NULL;
    /*
     * A PE32 guest can only name addresses below 4 GiB, so anything above the
     * 32-bit boundary is already a host pointer (an AIR metallib buffer handed
     * back through SM50_COMPILED_BITCODE for example, or a Wine heap
     * allocation).  Applying the arena bias to those would send the native
     * side to an unmapped address.
     */
    if (raw >= UINT64_C(0x100000000))
        return (void *)(uintptr_t)raw;
    function = madeira_guest_pointer_resolver();
    return function ? function(raw) : (void *)(uintptr_t)raw;
}

static inline uint64_t madeira_host_to_guest_pointer(const void *value)
{
    madeira_host_pointer_fn function;

    if (!value) return 0;
    function = madeira_host_pointer_resolver();
    return function ? function(value) : (uint64_t)(uintptr_t)value;
}

/* Some DXMT ABI entries carry a pointer in a plain uint64_t instead of a
 * WMTMemoryPointer (for example DispatchData_alloc_init and render-pass
 * descriptors).  Those values need exactly the same WoW64 translation. */
#define WMT_GUEST_RAW_PTR(value) \
    madeira_guest_to_host_pointer((const void *)(uintptr_t)(value))

/* The field argument is a WMTMemoryPointer/WMTConstMemoryPointer lvalue. */
#define WMT_GUEST_PTR(field) \
    madeira_guest_to_host_pointer((const void *)(uintptr_t)(field).ptr)
#define WMT_SET_GUEST_PTR(field, value) \
    ((field).ptr = (void *)(uintptr_t)madeira_host_to_guest_pointer((const void *)(value)))

#endif
