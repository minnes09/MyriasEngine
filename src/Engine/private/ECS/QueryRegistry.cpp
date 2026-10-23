#include "ECS/QueryRegistry.h"

namespace ECS_Private
{
void QueryRegistry::OnArchetypesCreated(ArchetypeSpan archetypes) noexcept
{
	// Not under queryMutex: structural changes are exclusive (nothing, not even a const query, runs
	// concurrently with them), so no reader can be looking at the caches or the lookup table.
	for (; seenArchetypes < archetypes.size(); ++seenArchetypes)
	{
		Archetype& archetype = *archetypes[seenArchetypes];
		for (const QueryCacheUnique& cache : ownedQueryCaches)
			if (Matches(*cache, archetype))
				AddArchetypeToCache(*cache, archetype);
	}
}

QueryCache& QueryRegistry::PublishQueryCache(QueryId id, QueryCache&& prototype, ArchetypeSpan archetypes) const noexcept
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

bool QueryRegistry::Matches(const QueryCache& cache, const Archetype& archetype) noexcept
{
	const ComponentMask& mask = archetype.Mask();
	return (mask & cache.required) == cache.required && !(mask & cache.excluded).any();
}

void QueryRegistry::AddArchetypeToCache(QueryCache& cache, Archetype& archetype) noexcept
{
	cache.archetypes.push_back(&archetype);
	for (const ComponentId component : cache.passed)
		cache.columns.push_back(archetype.ColumnIndex(component));
}
} // namespace ECS_Private
