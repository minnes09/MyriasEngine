#pragma once

#include "ECS/ChunkLayout.h"

#include <cstddef>

// Fixed-size block of component data. It holds only bytes: the owning Archetype lays out the
// entity column and one SoA column per component, and keeps the chunk's header (count, capacity)
// separately.
struct alignas(kChunkAlignment) Chunk
{
	std::byte bytes[kChunkSize];
};

static_assert(sizeof(Chunk) == kChunkSize);
