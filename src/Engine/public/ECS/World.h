#pragma once

#include "Core/Assert.h"
#include "ECS/Archetype.h"
#include "ECS/ChunkAllocator.h"
#include "ECS/ComponentType.h"
#include "ECS/EntityId.h"
#include "ECS/EntityIndex.h"
#include "ECS/Query.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <span>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace World_Private
{
// Tags have no storage: Add/Get on a tag hand out this shared empty instance.
template <typename T>
inline T tagInstance{};
} // namespace World_Private

// Owns entities and their components, stored in archetypes (one per component set) made of chunks.
// Any number of Worlds can be alive at once (up to 255): nothing here is global except the
// component type registry and the WorldId allocator.
//
// Thread safety: a const World allows concurrent reads (Has/TryGet/Get const, Query() const and
// iterating the queries). Structural changes (the immediate ones below and, from step 11, the
// deferred flush) are exclusive and single-thread per World. Invariant: a structural change must
// not overlap ANY access, not even const ones (Query() const, iteration, Get); the caller provides
// the synchronisation (a barrier or a join) between the structural phase and the read phase.
//
// The operations below are immediate structural changes: do not call Add/Remove/Destroy on this
// World while any of its queries is iterating (Debug asserts, per World; deferred mode, step 11,
// removes the restriction).
class World
{
public:
	World() noexcept;
	~World() noexcept;
	World(const World&)            = delete;
	World& operator=(const World&) = delete;
	World(World&&)                 = delete;
	World& operator=(World&&)      = delete;

	[[nodiscard]] WorldId GetWorldId() const noexcept { return worldId; }

	[[nodiscard]] EntityId CreateEntity() noexcept;
	// Stale ids are ignored; ids of another World assert.
	void                   DestroyEntity(EntityId entity) noexcept;
	[[nodiscard]] EntityId CloneEntity(EntityId source) noexcept;
	[[nodiscard]] bool     IsAlive(EntityId entity) const noexcept { return entityIndex.IsAlive(entity); }
	[[nodiscard]] std::size_t EntityCount() const noexcept { return entityIndex.AliveCount(); }

	// Adds T to the entity (moving it to another archetype). Asserts if T is already present,
	// then replaces the value.
	template <IsComponent T>
	T& Add(EntityId entity, T component = T{}) noexcept;
	// Removes T; no-op if absent.
	template <IsComponent T>
	void Remove(EntityId entity) noexcept;

	template <typename T>
		requires IsComponent<std::remove_cv_t<T>>
	[[nodiscard]] bool Has(EntityId entity) const noexcept;
	// Null if the entity is not alive or has no T.
	template <typename T>
		requires IsComponent<std::remove_cv_t<T>>
	[[nodiscard]] T* TryGet(EntityId entity) noexcept;
	template <typename T>
		requires IsComponent<std::remove_cv_t<T>>
	[[nodiscard]] const T* TryGet(EntityId entity) const noexcept;
	// Asserts if the entity is not alive or has no T.
	template <typename T>
		requires IsComponent<std::remove_cv_t<T>>
	[[nodiscard]] T& Get(EntityId entity) noexcept;
	template <typename T>
		requires IsComponent<std::remove_cv_t<T>>
	[[nodiscard]] const T& Get(EntityId entity) const noexcept;

	// Destroys every entity that has T.
	template <IsComponent T>
	void DestroyAllWith() noexcept;

	// Like vector::reserve: total capacity of the entity index (ids and records) for entityCount
	// entities. Chunks are NOT reserved: they depend on the archetypes the entities end up in.
	void Reserve(std::size_t entityCount) noexcept;
	// Destroys every entity; archetypes are kept for reuse.
	void Clear() noexcept;
	// Returns unused memory: spare chunks and fully free chunk blocks.
	void Trim() noexcept;

	[[nodiscard]] std::size_t ArchetypeCount() const noexcept { return archetypes.size(); }

	// Creates the cache of a query type ahead of time, so the first Query() in a frame does not
	// take the slow path (lock + allocation).
	template <typename... Terms>
	void PrepareQuery() const noexcept
	{
		static_cast<void>(MakeQuery<Terms...>());
	}

	// Cached query over the entities that match Terms (see Query.h for the term kinds). The first
	// call per query type (see PrepareQuery) builds its cache; later calls are a lock-free lookup.
	// Safe to call concurrently on a const World.
	// Inside World, the type is ::Query<...>.
	template <typename... Terms>
	[[nodiscard]] ::Query<Terms...> Query() noexcept
	{
		return MakeQuery<Terms...>();
	}
	// Read-only World: every passed term must be const T or Optional<const T>.
	template <typename... Terms>
	[[nodiscard]] ::Query<Terms...> Query() const noexcept
	{
		static_assert(((!Query_Private::TermTraits<Terms>::bPassed
		                || std::is_const_v<typename Query_Private::TermTraits<Terms>::Column>) && ...),
		              "Query on a const World: passed terms must be const T or Optional<const T>");
		return MakeQuery<Terms...>();
	}

private:
	void                   CheckWorld(EntityId entity) const noexcept;
	// Debug only (compiles to nothing otherwise): structural changes assert while a query iterates.
	void AssertNotIterating() const noexcept
	{
		MYRIAS_ASSERT(activeIterations.load(std::memory_order_relaxed) == 0,
		              "World: structural change while iterating a query");
	}
	// Slow path of MakeQuery: publishes the cache (filled with the existing archetypes) unless
	// another thread did it first.
	[[nodiscard]] QueryCache& PublishQueryCache(QueryId id, QueryCache&& prototype) const noexcept;
	[[nodiscard]] static bool Matches(const QueryCache& cache, const Archetype& archetype) noexcept;
	// Appends the archetype and its column indices (may allocate): structural path or cache creation only.
	static void               AddArchetypeToCache(QueryCache& cache, Archetype& archetype) noexcept;

	template <typename... Terms>
	[[nodiscard]] ::Query<Terms...> MakeQuery() const noexcept
	{
		const QueryId id    = GetQueryId<Terms...>();
		QueryCache*   cache = queryLookup[id].load(std::memory_order_acquire);
		if (!cache)
		{
			QueryCache prototype;
			(AddTermToCache<Terms>(prototype), ...);
			cache = &PublishQueryCache(id, std::move(prototype));
		}
		return ::Query<Terms...>(*cache, activeIterations);
	}

	template <typename Term>
	static void AddTermToCache(QueryCache& cache) noexcept
	{
		using Traits                = Query_Private::TermTraits<Term>;
		const ComponentId component = GetComponentId<typename Traits::Component>();
		if constexpr (Traits::bRequired)
			cache.required.set(component);
		if constexpr (Traits::bExcluded)
			cache.excluded.set(component);
		if constexpr (Traits::bPassed)
			cache.passed.push_back(component);
	}

	// bExisting: the component was already there (storage holds a live value). Otherwise storage is
	// raw memory for the new component (null for tags): the caller must construct it.
	struct AddedComponent
	{
		void* storage   = nullptr;
		bool  bExisting = false;
	};
	[[nodiscard]] AddedComponent AddComponent(EntityId entity, ComponentId component) noexcept;
	void                   RemoveComponent(EntityId entity, ComponentId component) noexcept;
	[[nodiscard]] bool     HasComponent(EntityId entity, ComponentId component) const noexcept;
	[[nodiscard]] void*    FindComponent(EntityId entity, ComponentId component) const noexcept;
	void                   DestroyAllWithComponent(ComponentId component) noexcept;
	void                   MoveEntity(EntityRecord& record, Archetype& target, const ComponentId* uninitialised) noexcept;
	[[nodiscard]] Archetype& AddTarget(Archetype& from, ComponentId component) noexcept;
	[[nodiscard]] Archetype& RemoveTarget(Archetype& from, ComponentId component) noexcept;
	[[nodiscard]] Archetype& FindOrCreateArchetype(std::span<const ComponentId> signature) noexcept;

	// Declaration order is destruction order in reverse: archetypes return their chunks to the
	// allocator before it goes away.
	WorldId                                       worldId;
	ChunkAllocator                                chunkAllocator;
	EntityIndex                                   entityIndex;
	std::vector<std::unique_ptr<Archetype>>       archetypes;
	std::unordered_map<ComponentMask, Archetype*> archetypesByMask; // structural lookups only
	Archetype*                                    emptyArchetype = nullptr;

	// Query caches. queryLookup[QueryId] is the lock-free fast path (null until the cache exists);
	// ownedQueryCaches owns them (stable addresses, freed with the World) and is the list the
	// World updates when it creates an archetype. queryMutex guards the creation of caches.
	// Mutable: a const World can be queried.
	mutable std::array<std::atomic<QueryCache*>, kMaxQueryTypes> queryLookup{};
	mutable std::vector<QueryCacheUnique>                        ownedQueryCaches;
	mutable std::mutex                                           queryMutex;
	// Running ForEach/ForEachChunk, used only by the Debug guard. Always declared (the layout does
	// not depend on MYRIAS_ASSERTS_ENABLED); untouched in Release.
	mutable std::atomic<std::uint32_t> activeIterations{ 0 };
};

template <IsComponent T>
T& World::Add(EntityId entity, T component) noexcept
{
	const AddedComponent added = AddComponent(entity, GetComponentId<T>());
	if constexpr (std::is_empty_v<T>)
		return World_Private::tagInstance<T>;
	else
	{
		T* slot = static_cast<T*>(added.storage);
		if (added.bExisting)
			std::destroy_at(slot); // already present (asserted): replace the value
		return *std::construct_at(slot, std::move(component)); // move constructor is noexcept (IsComponent)
	}
}

template <IsComponent T>
void World::Remove(EntityId entity) noexcept
{
	RemoveComponent(entity, GetComponentId<T>());
}

template <typename T>
	requires IsComponent<std::remove_cv_t<T>>
bool World::Has(EntityId entity) const noexcept
{
	return HasComponent(entity, GetComponentId<T>());
}

template <typename T>
	requires IsComponent<std::remove_cv_t<T>>
T* World::TryGet(EntityId entity) noexcept
{
	return const_cast<T*>(std::as_const(*this).TryGet<T>(entity));
}

template <typename T>
	requires IsComponent<std::remove_cv_t<T>>
const T* World::TryGet(EntityId entity) const noexcept
{
	using Type = std::remove_cv_t<T>;
	if constexpr (std::is_empty_v<Type>)
		return Has<Type>(entity) ? &World_Private::tagInstance<Type> : nullptr;
	else
		return static_cast<const T*>(FindComponent(entity, GetComponentId<Type>()));
}

template <typename T>
	requires IsComponent<std::remove_cv_t<T>>
T& World::Get(EntityId entity) noexcept
{
	return const_cast<T&>(std::as_const(*this).Get<T>(entity));
}

template <typename T>
	requires IsComponent<std::remove_cv_t<T>>
const T& World::Get(EntityId entity) const noexcept
{
	const T* component = TryGet<T>(entity);
	MYRIAS_ASSERT(component != nullptr, "World::Get: entity not alive or component missing");
	return *component;
}

template <IsComponent T>
void World::DestroyAllWith() noexcept
{
	DestroyAllWithComponent(GetComponentId<T>());
}
