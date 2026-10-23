#pragma once

#include <cstdint>

namespace ECS_Private
{
// Where an entity's components live inside an archetype.
struct ArchetypeRow
{
	std::uint32_t chunkIndex = 0;
	std::uint16_t row        = 0;

	friend constexpr bool operator==(ArchetypeRow, ArchetypeRow) noexcept = default;
};
} // namespace ECS_Private
