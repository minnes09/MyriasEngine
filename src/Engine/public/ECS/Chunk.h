#pragma once

#include "ECS/EntityId.h"

#include <cstddef>

inline constexpr std::size_t kChunkSize      = 16 * 1024;
inline constexpr std::size_t kChunkAlignment = 64; // cache line: every column starts aligned

[[nodiscard]] constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept
{
	return (value + alignment - 1) / alignment * alignment;
}

// Where the first column starts in a chunk holding a single row (the entity ids come first).
inline constexpr std::size_t kFirstColumnOffset = AlignUp(sizeof(EntityId), kChunkAlignment);

// Fixed-size block of component data. It holds only bytes: the owning Archetype lays out the
// entity column and one SoA column per component, and keeps the chunk's header (count, capacity)
// separately.
struct alignas(kChunkAlignment) Chunk
{
	std::byte bytes[kChunkSize];
};

static_assert(sizeof(Chunk) == kChunkSize);
