#pragma once

#include "Core/Alignment.h"
#include "ECS/EntityId.h"

#include <cstddef>

// Geometry of a chunk. The Chunk type itself is internal to the ECS module.
inline constexpr std::size_t kChunkSize      = 16 * 1024;
inline constexpr std::size_t kChunkAlignment = 64; // cache line: every column starts aligned

// Where the first column starts in a chunk holding a single row (the entity ids come first).
inline constexpr std::size_t kFirstColumnOffset = AlignUp(sizeof(EntityId), kChunkAlignment);
