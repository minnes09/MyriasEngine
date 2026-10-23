#pragma once

#include "ECS/ComponentId.h"

#include <memory>
#include <vector>

namespace ECS_Private
{
class Archetype;

// Per-World, per-query-type cache: the archetypes that match, with the column of every passed
// term already resolved. Internal to World/Query. Updated eagerly by the World when it creates an
// archetype (structural, single-thread), so reading it never writes: concurrent reads are safe.
struct QueryCache
{
	ComponentMask            required;
	ComponentMask            excluded;
	std::vector<ComponentId> passed;            // ComponentId of every passed term, in term order
	std::vector<Archetype*>  archetypes;        // matching archetypes, in creation order
	std::vector<int>         columns;           // archetypes.size() * passed.size(): column index, -1 if absent
};

using QueryCacheUnique = std::unique_ptr<QueryCache>;
} // namespace ECS_Private
