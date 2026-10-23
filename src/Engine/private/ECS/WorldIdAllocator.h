#pragma once

#include "ECS/WorldId.h"

// Process-wide allocator of WorldIds (1..255). Internal to the ECS module.
// Ids are handed out round-robin, so a just-released id is not reused immediately: a stale
// EntityId from a destroyed World is unlikely to match a new World.
namespace WorldIdAllocator
{
// Aborts (also in Release) if 255 Worlds are already alive.
[[nodiscard]] WorldId Acquire() noexcept;
void                  Release(WorldId world) noexcept;
} // namespace WorldIdAllocator
