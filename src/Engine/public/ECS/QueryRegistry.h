#pragma once

#include "ECS/Archetype.h"
#include "ECS/ComponentId.h"
#include "ECS/QueryCache.h"
#include "ECS/QueryId.h"
#include "ECS/QueryTerms.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <span>
#include <utility>
#include <vector>

namespace ECS_Private
{
// Owns the query caches of a World. Knows nothing about the archetype graph: it receives the
// archetypes as a span (creation order, append only) when a cache is created and, through
// OnArchetypesCreated, whenever the World reports that new ones exist.
//
// Thread safety: Find and Publish are const and safe to call concurrently (lock-free fast path, mutex on the
// slow path); the lookup table, the owned list and the mutex are mutable for that reason, so the
// World holds the registry as a plain member. OnArchetypesCreated is structural: exclusive, like
// every structural change of the World.
class QueryRegistry
{
public:
	using ArchetypeSpan = std::span<const std::unique_ptr<Archetype>>;

	QueryRegistry() noexcept                       = default;
	~QueryRegistry() noexcept                      = default;
	QueryRegistry(const QueryRegistry&)            = delete;
	QueryRegistry& operator=(const QueryRegistry&) = delete;
	QueryRegistry(QueryRegistry&&)                 = delete;
	QueryRegistry& operator=(QueryRegistry&&)      = delete;

	// Adds to the matching caches every archetype past the ones already seen. Call after any
	// operation that may have created archetypes (cheap when there is nothing new). Eager: queries
	// never scan on read.
	// Cheap inline test for OnArchetypesCreated.
	[[nodiscard]] bool HasUnseenArchetypes(ArchetypeSpan archetypes) const noexcept { return seenArchetypes < archetypes.size(); }
	void OnArchetypesCreated(ArchetypeSpan archetypes) noexcept;

	// Lock-free fast path: the cache of a query type, or null if it does not exist yet.
	[[nodiscard]] QueryCache* Find(QueryId id) const noexcept { return queryLookup[id].load(std::memory_order_acquire); }

	// Slow path (first use of a query type): builds the cache of Terms and publishes it, filled with
	// archetypes (all the existing ones). Returns the existing cache if another thread was faster.
	template <typename... Terms>
	[[nodiscard]] QueryCache& Publish(QueryId id, ArchetypeSpan archetypes) const noexcept
	{
		QueryCache prototype;
		(AddTermToCache<Terms>(prototype), ...);
		return PublishQueryCache(id, std::move(prototype), archetypes);
	}

private:
	// Non-template part of Publish: publishes the cache (filled with the existing archetypes) unless
	// another thread did it first.
	[[nodiscard]] QueryCache& PublishQueryCache(QueryId id, QueryCache&& prototype, ArchetypeSpan archetypes) const noexcept;
	[[nodiscard]] static bool Matches(const QueryCache& cache, const Archetype& archetype) noexcept;
	// Appends the archetype and its column indices (may allocate): structural path or cache creation only.
	static void AddArchetypeToCache(QueryCache& cache, Archetype& archetype) noexcept;

	template <typename Term>
	static void AddTermToCache(QueryCache& cache) noexcept
	{
		using Traits                = TermTraits<Term>;
		const ComponentId component = GetComponentId<typename Traits::Component>();
		if constexpr (Traits::bRequired)
			cache.required.set(component);
		if constexpr (Traits::bExcluded)
			cache.excluded.set(component);
		if constexpr (Traits::bPassed)
			cache.passed.push_back(component);
	}

	// queryLookup[QueryId] is the lock-free fast path (null until the cache exists); ownedQueryCaches
	// owns them (stable addresses, freed with the registry) and is the list updated when archetypes
	// are created. queryMutex guards the creation of caches.
	mutable std::array<std::atomic<QueryCache*>, kMaxQueryTypes> queryLookup{};
	mutable std::vector<QueryCacheUnique>                        ownedQueryCaches;
	mutable std::mutex                                           queryMutex;
	std::size_t                                                  seenArchetypes = 0;
};
} // namespace ECS_Private
