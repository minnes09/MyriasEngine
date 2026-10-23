#include "ECS/World.h"

#include "Core/Fatal.h"
#include "ECS/Archetype.h"
#include "ECS/ArchetypeGraph.h"
#include "ECS/WorldIdAllocator.h"

#include <cstdint>
#include <memory>


using namespace ECS_Private;

World::World() noexcept
    : worldId(WorldIdAllocator::Acquire())
    , entityIndex(worldId)
    , archetypeGraph(chunkAllocator)
{
	SyncQueryRegistry();
}

World::~World() noexcept
{
	WorldIdAllocator::Release(worldId);
}

EntityId World::CreateEntity() noexcept
{
	AssertNotIterating();
	const EntityId     entity = entityIndex.Create();
	const ArchetypeRow row    = archetypeGraph.Empty().AddRow(entity);
	entityIndex.Get(entity)   = { .archetype = &archetypeGraph.Empty(), .row = row };
	return entity;
}

void World::DestroyEntity(EntityId entity) noexcept
{
	AssertNotIterating();
	CheckWorld(entity);
	EntityRecord* record = entityIndex.Find(entity);
	if (!record)
		return; // stale id

	const ArchetypeRow row   = record->row;
	const EntityId     moved = record->archetype->RemoveRow(row);
	if (!moved.IsNull())
		entityIndex.Get(moved).row = row;
	entityIndex.Destroy(entity);
}

EntityId World::CloneEntity(EntityId source) noexcept
{
	AssertNotIterating();
	CheckWorld(source);
	// Always checked (ADR-0005, strict exception): a stale id whose slot was reused would clone another entity.
	const EntityRecord* found = entityIndex.Find(source);
	if (!found)
		FatalError("World::CloneEntity: entity is not alive (stale, null or destroyed id, or id of another World)");
	const EntityRecord original = *found; // copied: Create writes other slots (pages never move, so a reference would also work)

	const EntityId     clone = entityIndex.Create();
	const ArchetypeRow row   = original.archetype->CloneRow(original.row, clone);
	entityIndex.Get(clone)   = { .archetype = original.archetype, .row = row };
	return clone;
}

void World::Reserve(std::size_t entityCount) noexcept
{
	entityIndex.Reserve(entityCount);
}

void World::Clear() noexcept
{
	AssertNotIterating();
	for (const std::unique_ptr<Archetype>& archetype : archetypeGraph.Archetypes())
	{
		while (archetype->EntityCount() > 0)
		{
			const auto     lastChunk = static_cast<std::uint32_t>(archetype->Chunks().size() - 1);
			const EntityId last      = archetype->Entities(lastChunk).back();
			DestroyEntity(last); // last row: nothing is moved
		}
	}
}

void World::Trim() noexcept
{
	for (const std::unique_ptr<Archetype>& archetype : archetypeGraph.Archetypes())
		archetype->ReleaseSpareChunk();
	chunkAllocator.Trim();
}

void World::CheckWorld(EntityId entity) const noexcept
{
	MYRIAS_ASSERT(entity.IsNull() || entity.GetWorldId() == worldId, "EntityId belongs to another World");
}

World::AddedComponent World::AddComponent(EntityId entity, ComponentId component) noexcept
{
	AssertNotIterating();
	CheckWorld(entity);
	// Always checked (ADR-0005, strict exception): a stale id whose slot was reused would write to another entity.
	EntityRecord* found = entityIndex.Find(entity);
	if (!found)
		FatalError("World::Add: entity is not alive (stale, null or destroyed id, or id of another World)");
	EntityRecord& record = *found;

	if (record.archetype->Mask().test(component))
	{
		MYRIAS_ASSERT(false, "World::Add: component already present");
		const int column = record.archetype->ColumnIndex(component);
		return { .storage = column >= 0 ? record.archetype->ComponentData(record.row, column) : nullptr, .bExisting = true };
	}

	Archetype& target = archetypeGraph.AddTarget(*record.archetype, component);
	SyncQueryRegistry();
	const int  column = target.ColumnIndex(component);
	if (column < 0) // tag: nothing to construct
	{
		MoveEntity(record, target, nullptr);
		return { .storage = nullptr, .bExisting = false };
	}
	MoveEntity(record, target, &component); // the new column stays raw: Add constructs the value in place
	return { .storage = record.archetype->ComponentData(record.row, column), .bExisting = false };
}

void World::RemoveComponent(EntityId entity, ComponentId component) noexcept
{
	AssertNotIterating();
	CheckWorld(entity);
	EntityRecord* record = entityIndex.Find(entity);
	if (!record || !record->archetype->Mask().test(component))
		return;
	Archetype& target = archetypeGraph.RemoveTarget(*record->archetype, component);
	SyncQueryRegistry();
	MoveEntity(*record, target, nullptr);
}

bool World::HasComponent(EntityId entity, ComponentId component) const noexcept
{
	CheckWorld(entity);
	const EntityRecord* record = entityIndex.Find(entity);
	return record && record->archetype->Mask().test(component);
}

void* World::FindComponent(EntityId entity, ComponentId component) const noexcept
{
	CheckWorld(entity);
	const EntityRecord* record = entityIndex.Find(entity);
	if (!record)
		return nullptr;
	const int column = record->archetype->ColumnIndex(component);
	return column >= 0 ? record->archetype->ComponentData(record->row, column) : nullptr;
}

void World::DestroyAllWithComponent(ComponentId component) noexcept
{
	AssertNotIterating();
	for (const std::unique_ptr<Archetype>& archetype : archetypeGraph.Archetypes())
	{
		if (!archetype->Mask().test(component))
			continue;
		while (archetype->EntityCount() > 0)
		{
			const auto lastChunk = static_cast<std::uint32_t>(archetype->Chunks().size() - 1);
			DestroyEntity(archetype->Entities(lastChunk).back()); // last row: nothing is moved
		}
	}
}

void World::MoveEntity(EntityRecord& record, Archetype& target, const ComponentId* uninitialised) noexcept
{
	const ArchetypeMove move = uninitialised ? record.archetype->MoveRowUninitialised(record.row, target, *uninitialised)
	                                         : record.archetype->MoveRow(record.row, target);
	if (!move.movedEntity.IsNull())
		entityIndex.Get(move.movedEntity).row = record.row; // it took the vacated row
	record = { .archetype = &target, .row = move.destination };
}
