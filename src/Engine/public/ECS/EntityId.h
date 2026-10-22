#pragma once

#include "Core/Assert.h"

#include <cstddef>
#include <cstdint>
#include <functional>

// Identifies a World: 1..255, 0 = no world.
using WorldId = std::uint8_t;

// Identifies an entity inside a World. 64 bits:
//   bits  0-31  index       (slot in the World's entity index)
//   bits 32-47  generation  (reuse counter: detects stale ids; 0 is reserved)
//   bits 48-55  world       (detects ids used in the wrong World)
//   bits 56-63  flags       (reserved for relationships/toggles; 0 today, ignored by ==)
// The default value is null; a valid id is never 0.
class EntityId
{
public:
	constexpr EntityId() noexcept = default;

	[[nodiscard]] static constexpr EntityId Create(std::uint32_t index, std::uint16_t generation, WorldId world,
	                                               std::uint8_t flags = 0) noexcept
	{
		MYRIAS_ASSERT(generation != 0, "EntityId: generation 0 is reserved");
		MYRIAS_ASSERT(world != 0, "EntityId: WorldId 0 means no world");
		EntityId id;
		id.value = std::uint64_t{ index } | (std::uint64_t{ generation } << kGenerationShift)
		         | (std::uint64_t{ world } << kWorldShift) | (std::uint64_t{ flags } << kFlagsShift);
		return id;
	}

	[[nodiscard]] constexpr std::uint32_t Index() const noexcept { return static_cast<std::uint32_t>(value); }
	[[nodiscard]] constexpr std::uint16_t Generation() const noexcept
	{
		return static_cast<std::uint16_t>(value >> kGenerationShift);
	}
	[[nodiscard]] constexpr WorldId      GetWorldId() const noexcept { return static_cast<WorldId>(value >> kWorldShift); }
	[[nodiscard]] constexpr std::uint8_t Flags() const noexcept { return static_cast<std::uint8_t>(value >> kFlagsShift); }
	[[nodiscard]] constexpr bool         IsNull() const noexcept { return (value & kIdentityMask) == 0; }

	friend constexpr bool operator==(EntityId lhs, EntityId rhs) noexcept
	{
		return (lhs.value & kIdentityMask) == (rhs.value & kIdentityMask);
	}

private:
	friend struct std::hash<EntityId>;

	static constexpr unsigned      kGenerationShift = 32;
	static constexpr unsigned      kWorldShift      = 48;
	static constexpr unsigned      kFlagsShift      = 56;
	static constexpr std::uint64_t kIdentityMask    = (std::uint64_t{ 1 } << kFlagsShift) - 1;

	std::uint64_t value = 0;
};

template <>
struct std::hash<EntityId>
{
	// Consistent with ==: flags are masked out.
	[[nodiscard]] std::size_t operator()(EntityId id) const noexcept
	{
		return std::hash<std::uint64_t>{}(id.value & EntityId::kIdentityMask);
	}
};
