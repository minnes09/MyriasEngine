#include "ECS/EntityIndex.h"

#include "Core/Assert.h"
#include "Core/Fatal.h"
#include "ECS/Archetype.h"

#include <limits>
#include <utility>

namespace ECS_Private
{
EntityIndex::EntityIndex(WorldId world) noexcept
    : world(world)
{
	MYRIAS_ASSERT(world != 0, "EntityIndex: WorldId 0 means no world");
}

EntityIndex::~EntityIndex() noexcept = default;

EntityId EntityIndex::Create() noexcept
{
	if (aliveCount < dense.size())
	{
		// Recycle: the free id already carries its next generation.
		const EntityId entity                = dense[aliveCount];
		FindSlot(entity.Index())->denseIndex = static_cast<std::uint32_t>(aliveCount);
		FindSlot(entity.Index())->record     = {};
		++aliveCount;
		return entity;
	}

	// New index from a dedicated counter: retired slots make dense.size() smaller than the indices used.
	if (nextIndex == std::numeric_limits<std::uint32_t>::max()) // always checked
		FatalError("EntityIndex: out of entity indices");
	const std::uint32_t index  = nextIndex++;
	const EntityId      entity = EntityId::Create(index, 1, world);
	SlotFor(index)        = { .record = {}, .denseIndex = static_cast<std::uint32_t>(dense.size()) };
	dense.push_back(entity);
	++aliveCount;
	return entity;
}

void EntityIndex::Destroy(EntityId entity) noexcept
{
	MYRIAS_ASSERT(IsAlive(entity), "EntityIndex::Destroy: entity is not alive");

	Slot&               slot      = *FindSlot(entity.Index());
	const std::uint32_t lastAlive = static_cast<std::uint32_t>(aliveCount - 1);

	// Swap with the last alive id so the alive range stays packed.
	const EntityId last                = dense[lastAlive];
	dense[slot.denseIndex]             = last;
	FindSlot(last.Index())->denseIndex = slot.denseIndex;
	--aliveCount;

	slot.record = {};
	if (entity.Generation() == std::numeric_limits<std::uint16_t>::max())
	{
		// Retire: a wrapped generation would make stale ids valid again.
		dense[lastAlive] = dense.back();
		if (lastAlive != dense.size() - 1)
			FindSlot(dense[lastAlive].Index())->denseIndex = lastAlive;
		dense.pop_back();
		slot.denseIndex = kRetired;
		return;
	}

	dense[lastAlive] = EntityId::Create(entity.Index(), static_cast<std::uint16_t>(entity.Generation() + 1), world);
	slot.denseIndex  = lastAlive;
}

bool EntityIndex::IsAlive(EntityId entity) const noexcept
{
	return Find(entity) != nullptr;
}

EntityRecord* EntityIndex::Find(EntityId entity) noexcept
{
	return const_cast<EntityRecord*>(std::as_const(*this).Find(entity));
}

const EntityRecord* EntityIndex::Find(EntityId entity) const noexcept
{
	const Slot* slot = FindSlot(entity.Index());
	return slot && slot->denseIndex < aliveCount && dense[slot->denseIndex] == entity ? &slot->record : nullptr;
}

EntityRecord& EntityIndex::Get(EntityId entity) noexcept
{
	MYRIAS_ASSERT(IsAlive(entity), "EntityIndex::Get: entity is not alive");
	return FindSlot(entity.Index())->record;
}

void EntityIndex::Reserve(std::size_t entityCount) noexcept
{
	dense.reserve(entityCount);
	const std::size_t pageCount = (entityCount + kPageSize - 1) / kPageSize;
	pages.reserve(pageCount);
	if (pages.size() < pageCount)
		pages.resize(pageCount);
	for (std::size_t i = 0; i < pageCount; ++i)
		if (!pages[i])
			pages[i] = std::make_unique<Page>();
}

void EntityIndex::Clear() noexcept
{
	while (aliveCount > 0)
		Destroy(dense[aliveCount - 1]);
}

EntityIndex::Slot* EntityIndex::FindSlot(std::uint32_t index) noexcept
{
	return const_cast<Slot*>(std::as_const(*this).FindSlot(index));
}

const EntityIndex::Slot* EntityIndex::FindSlot(std::uint32_t index) const noexcept
{
	const std::size_t page = index / kPageSize;
	if (page >= pages.size() || !pages[page])
		return nullptr;
	return &(*pages[page])[index % kPageSize];
}

EntityIndex::Slot& EntityIndex::SlotFor(std::uint32_t index) noexcept
{
	const std::size_t page = index / kPageSize;
	if (page >= pages.size())
		pages.resize(page + 1);
	if (!pages[page])
		pages[page] = std::make_unique<Page>();
	return (*pages[page])[index % kPageSize];
}
} // namespace ECS_Private
