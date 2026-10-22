#pragma once

#include "ECS/Archetype.h"
#include "ECS/EntityId.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Where an entity's components are stored.
struct EntityRecord
{
	// Null only in the future deferred mode, for an entity that is alive but not placed in an
	// archetype yet. The immediate API (World::CreateEntity/CloneEntity) always places it.
	Archetype*   archetype = nullptr;
	ArchetypeRow row;
};

// Per-World map from EntityId to EntityRecord, with index recycling (dense/sparse, as in flecs):
// - dense: alive ids first, then free ids already carrying their next generation;
// - sparse: pages of records indexed by the entity index, created on first use.
// Create/Destroy/lookup are O(1) and do not allocate once warmed up.
// A slot whose generation reaches its maximum is retired and never reused.
class EntityIndex
{
public:
	explicit EntityIndex(WorldId world) noexcept;
	~EntityIndex() noexcept;
	EntityIndex(const EntityIndex&)            = delete;
	EntityIndex& operator=(const EntityIndex&) = delete;

	[[nodiscard]] EntityId Create() noexcept;
	void                   Destroy(EntityId entity) noexcept;
	// True only if index, generation and World all match a living entity.
	[[nodiscard]] bool     IsAlive(EntityId entity) const noexcept;

	[[nodiscard]] EntityRecord*       Find(EntityId entity) noexcept;
	[[nodiscard]] const EntityRecord* Find(EntityId entity) const noexcept;
	[[nodiscard]] EntityRecord&       Get(EntityId entity) noexcept;

	[[nodiscard]] std::size_t AliveCount() const noexcept { return aliveCount; }
	[[nodiscard]] WorldId     GetWorldId() const noexcept { return world; }

	// Like vector::reserve: total capacity. Reserves the dense array and creates the pages for
	// the indices [0, entityCount), so creating entities up to that total does not allocate.
	void Reserve(std::size_t entityCount) noexcept;
	// Every entity dies; their indices are reused by later Create calls.
	void Clear() noexcept;

private:
	static constexpr std::size_t   kPageSize = 4096;
	static constexpr std::uint32_t kRetired  = 0xFFFFFFFFu;

	struct Slot
	{
		EntityRecord  record;
		std::uint32_t denseIndex = kRetired;
	};
	using Page = std::array<Slot, kPageSize>;

	[[nodiscard]] Slot*       FindSlot(std::uint32_t index) noexcept;
	[[nodiscard]] const Slot* FindSlot(std::uint32_t index) const noexcept;
	[[nodiscard]] Slot&       SlotFor(std::uint32_t index) noexcept;

	WorldId                            world;
	std::vector<EntityId>              dense;
	std::vector<std::unique_ptr<Page>> pages;
	std::size_t                        aliveCount = 0;
	std::uint32_t                      nextIndex  = 0; // first never-used index
};
