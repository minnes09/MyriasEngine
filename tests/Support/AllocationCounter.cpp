#include "Support/AllocationCounter.h"

#include <cstdio>
#include <cstdlib>
#include <new>

namespace AllocationCounter_Private
{
namespace
{
thread_local bool        bCounting       = false;
thread_local std::size_t allocationCount = 0;

void* Allocate(std::size_t size) noexcept
{
	if (bCounting)
		++allocationCount;
	return std::malloc(size != 0 ? size : 1);
}

void* AllocateAligned(std::size_t size, std::align_val_t alignment) noexcept
{
	if (bCounting)
		++allocationCount;
	const auto align = static_cast<std::size_t>(alignment);
#if defined(_WIN32)
	return _aligned_malloc(size != 0 ? size : 1, align);
#else
	const std::size_t rounded = ((size != 0 ? size : 1) + align - 1) / align * align;
	return std::aligned_alloc(align, rounded);
#endif
}

void FreeAligned(void* pointer) noexcept
{
#if defined(_WIN32)
	_aligned_free(pointer);
#else
	std::free(pointer);
#endif
}

void* AllocateOrThrow(std::size_t size)
{
	if (void* pointer = Allocate(size))
		return pointer;
	throw std::bad_alloc();
}

void* AllocateAlignedOrThrow(std::size_t size, std::align_val_t alignment)
{
	if (void* pointer = AllocateAligned(size, alignment))
		return pointer;
	throw std::bad_alloc();
}
} // namespace
} // namespace AllocationCounter_Private

AllocationScope::AllocationScope() noexcept
{
	// Always checked, also in Release: a nested scope would reset the outer count and, on exit,
	// stop counting for the rest of the outer scope.
	if (AllocationCounter_Private::bCounting)
	{
		std::fputs("AllocationScope: scopes cannot be nested\n", stderr);
		std::abort();
	}
	AllocationCounter_Private::allocationCount = 0;
	AllocationCounter_Private::bCounting       = true;
}

AllocationScope::~AllocationScope() noexcept
{
	AllocationCounter_Private::bCounting = false;
}

std::size_t AllocationScope::Count() const noexcept
{
	return AllocationCounter_Private::allocationCount;
}

// Replaceable global allocation functions ([new.delete]). Every form is replaced so that no
// allocation path bypasses the counter and no pointer is freed by a mismatched allocator.

void* operator new(std::size_t size) { return AllocationCounter_Private::AllocateOrThrow(size); }
void* operator new[](std::size_t size) { return AllocationCounter_Private::AllocateOrThrow(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept { return AllocationCounter_Private::Allocate(size); }
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept { return AllocationCounter_Private::Allocate(size); }

void* operator new(std::size_t size, std::align_val_t alignment)
{
	return AllocationCounter_Private::AllocateAlignedOrThrow(size, alignment);
}
void* operator new[](std::size_t size, std::align_val_t alignment)
{
	return AllocationCounter_Private::AllocateAlignedOrThrow(size, alignment);
}
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	return AllocationCounter_Private::AllocateAligned(size, alignment);
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
	return AllocationCounter_Private::AllocateAligned(size, alignment);
}

void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete(void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }

void operator delete(void* pointer, std::align_val_t) noexcept { AllocationCounter_Private::FreeAligned(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { AllocationCounter_Private::FreeAligned(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { AllocationCounter_Private::FreeAligned(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { AllocationCounter_Private::FreeAligned(pointer); }
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { AllocationCounter_Private::FreeAligned(pointer); }
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { AllocationCounter_Private::FreeAligned(pointer); }
