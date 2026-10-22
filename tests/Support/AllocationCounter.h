#pragma once

#include <cstddef>

// Counts heap allocations made by the current thread while a scope is alive. Backed by a global
// operator new/delete replacement linked into the test executable (AllocationCounter.cpp).
//
// Keep assertions OUTSIDE the scope: a failing EXPECT allocates.
// Scopes cannot be nested (they share one per-thread counter): a nested one aborts.
class AllocationScope
{
public:
	AllocationScope() noexcept;
	~AllocationScope() noexcept;
	AllocationScope(const AllocationScope&)            = delete;
	AllocationScope& operator=(const AllocationScope&) = delete;

	[[nodiscard]] std::size_t Count() const noexcept;
};
