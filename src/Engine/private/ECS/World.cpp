#include "ECS/World.h"

#include "Core/Fatal.h"
#include "WorldIdAllocator.h"

#include <algorithm>
#include <iterator>

World::World() noexcept
    : worldId(WorldIdAllocator::Acquire())
    , entityIndex(worldId)
{
	emptyArchetype = &FindOrCreateArchetype({});
}

World::~World() noexcept
{
	archetypesByMask.clear();
	archetypes.clear(); // destroys components and returns chunks while the allocator is alive
	WorldIdAllocator::Release(worldId);
}

EntityId World::CreateEntity() noexcept
{
	AssertNotIterating();
	const EntityId     entity = entityIndex.Create();
	const ArchetypeRow row    = emptyArchetype->AddRow(entity);
	entityIndex.Get(entity)   = { .archetype = emptyArchetype, .row = row };
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
	for (const std::unique_ptr<Archetype>& archetype : archetypes)
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
	for (const std::unique_ptr<Archetype>& archetype : archetypes)
		archetype->ReleaseSpareChunk();
	chunkAllocator.Trim();
}

QueryCache& World::PublishQueryCache(QueryId id, QueryCache&& prototype) const noexcept
{
	const std::scoped_lock lock(queryMutex);
	if (QueryCache* existing = queryLookup[id].load(std::memory_order_relaxed))
		return *existing; // another thread was faster

	QueryCacheUnique cache = std::make_unique<QueryCache>(std::move(prototype));
	for (const std::unique_ptr<Archetype>& archetype : archetypes)
		if (Matches(*cache, *archetype))
			AddArchetypeToCache(*cache, *archetype);
	QueryCache* const raw = cache.get();
	ownedQueryCaches.push_back(std::move(cache));
	queryLookup[id].store(raw, std::memory_order_release);
	return *raw;
}

bool World::Matches(const QueryCache& cache, const Archetype& archetype) noexcept
{
	const ComponentMask& mask = archetype.Mask();
	return (mask & cache.required) == cache.required && !(mask & cache.excluded).any();
}

void World::AddArchetypeToCache(QueryCache& cache, Archetype& archetype) noexcept
{
	cache.archetypes.push_back(&archetype);
	for (const ComponentId component : cache.passed)
		cache.columns.push_back(archetype.ColumnIndex(component));
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

	Archetype& target = AddTarget(*record.archetype, component);
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
	MoveEntity(*record, RemoveTarget(*record->archetype, component), nullptr);
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
	for (const std::unique_ptr<Archetype>& archetype : archetypes)
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

Archetype& World::AddTarget(Archetype& from, ComponentId component) noexcept
{
	if (const ArchetypeTransition* transition = from.FindTransition(component); transition && transition->onAdd)
		return *transition->onAdd;

	std::vector<ComponentId> signature(from.Signature().begin(), from.Signature().end());
	signature.insert(std::ranges::upper_bound(signature, component), component);
	Archetype& target = FindOrCreateArchetype(signature);

	from.GetOrAddTransition(component).onAdd      = &target;
	target.GetOrAddTransition(component).onRemove = &from;
	return target;
}

Archetype& World::RemoveTarget(Archetype& from, ComponentId component) noexcept
{
	if (const ArchetypeTransition* transition = from.FindTransition(component); transition && transition->onRemove)
		return *transition->onRemove;

	std::vector<ComponentId> signature;
	std::ranges::copy_if(from.Signature(), std::back_inserter(signature), [&](ComponentId id) { return id != component; });
	Archetype& target = FindOrCreateArchetype(signature);

	from.GetOrAddTransition(component).onRemove = &target;
	target.GetOrAddTransition(component).onAdd  = &from;
	return target;
}

Archetype& World::FindOrCreateArchetype(std::span<const ComponentId> signature) noexcept
{
	ComponentMask mask;
	for (const ComponentId component : signature)
		mask.set(component);

	if (const auto it = archetypesByMask.find(mask); it != archetypesByMask.end())
		return *it->second;

	// Not under queryMutex: structural changes are exclusive (nothing, not even a const query, runs
	// concurrently with them), so no reader can be looking at the caches or the lookup table.
	std::unique_ptr<Archetype> owned     = std::make_unique<Archetype>(signature, chunkAllocator);
	Archetype&                 archetype = *owned;
	archetypes.push_back(std::move(owned));
	archetypesByMask.emplace(mask, &archetype);
	for (const QueryCacheUnique& cache : ownedQueryCaches)
		if (Matches(*cache, archetype))
			AddArchetypeToCache(*cache, archetype); // eager: queries never scan on read
	return archetype;
}
