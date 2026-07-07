#include <stddef.h>
#include <stdlib.h>

#include <SDL3/SDL.h>
#include <Volk/volk.h>

#ifndef _MSC_VER
void* operator new(size_t size) {
    void* result = malloc(size);
    if (!result) {
        abort();
    }

    return result;
}

void* operator new[](size_t size) {
    void* result = malloc(size);
    if (!result) {
        abort();
    }

    return result;
}

void operator delete(void* ptr) noexcept {
    free(ptr);
}

void operator delete[](void* ptr) noexcept {
    free(ptr);
}

#ifdef __clang__
extern "C" void __cxa_pure_virtual(void) {
    SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Pure virtual function called.");
    abort();
}
#endif

static void* nc__vma_aligned_malloc(const size_t size, size_t alignment) {
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }

    void* result = NULL;
    return posix_memalign(&result, alignment, size) == 0 ? result : NULL;
}

static void nc__vma_aligned_free(void* ptr) {
    free(ptr);
}

#define VMA_SYSTEM_ALIGNED_MALLOC(size, alignment) nc__vma_aligned_malloc((size), (alignment))
#define VMA_SYSTEM_ALIGNED_FREE(ptr) nc__vma_aligned_free((ptr))

class nc__vma_mutex_t {
public:
    nc__vma_mutex_t() {
        mutex = SDL_CreateMutex();
        if (!mutex) {
            SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Failed to create VMA mutex: %s", SDL_GetError());
            abort();
        }
    }

    ~nc__vma_mutex_t() {
        SDL_DestroyMutex(mutex);
    }

    void Lock() {
        SDL_LockMutex(mutex);
    }

    bool TryLock() {
        return SDL_TryLockMutex(mutex);
    }

    void Unlock() {
        SDL_UnlockMutex(mutex);
    }

private:
    SDL_Mutex* mutex;
};

#define VMA_USE_STL_SHARED_MUTEX 0
#define VMA_MUTEX nc__vma_mutex_t
#endif

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_VULKAN_VERSION 1001000
#define VMA_NULLABLE
#define VMA_NOT_NULL
#define VMA_NULLABLE_NON_DISPATCHABLE
#define VMA_NOT_NULL_NON_DISPATCHABLE
#define VMA_IMPLEMENTATION
#ifdef _MSC_VER
#pragma warning(push, 0)
#pragma warning(disable: 4127)
#pragma warning(disable: 4189)
#pragma warning(disable: 4324)
#pragma warning(disable: 4505)
#pragma warning(disable: 4701)
#pragma warning(disable: 4703)
#if __has_include(<vk_mem_alloc.h>)
#include <vk_mem_alloc.h>
#else
#include <vma/vk_mem_alloc.h>
#endif
#pragma warning(pop)
#else
#if __has_include(<vk_mem_alloc.h>)
#include <vk_mem_alloc.h>
#else
#include <vma/vk_mem_alloc.h>
#endif
#endif
