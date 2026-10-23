#include "ECS/ArchetypeGraph.h"

#include "ECS/ChunkAllocator.h"

#include <algorithm>
#include <iterator>
#include <vector>

namespace ECS_Private
{
ArchetypeGraph::ArchetypeGraph(ChunkAllocator& chunkAllocator) noexcept
    : chunkAllocator(chunkAllocator)
{
	emptyArchetype = &FindOrCreate({});
}

ArchetypeGraph::~ArchetypeGraph() noexcept
{
	archetypesByMask.clear();
	archetypes.clear(); // destroys components and returns chunks while the allocator is alive
}

Archetype& ArchetypeGraph::AddTarget(Archetype& from, ComponentId component) noexcept
{
	if (const ArchetypeTransition* transition = from.FindTransition(component); transition && transition->onAdd)
		return *transition->onAdd;

	std::vector<ComponentId> signature(from.Signature().begin(), from.Signature().end());
	signature.insert(std::ranges::upper_bound(signature, component), component);
	Archetype& target = FindOrCreate(signature);

	from.GetOrAddTransition(component).onAdd      = &target;
	target.GetOrAddTransition(component).onRemove = &from;
	return target;
}

Archetype& ArchetypeGraph::RemoveTarget(Archetype& from, ComponentId component) noexcept
{
	if (const ArchetypeTransition* transition = from.FindTransition(component); transition && transition->onRemove)
		return *transition->onRemove;

	std::vector<ComponentId> signature;
	std::ranges::copy_if(from.Signature(), std::back_inserter(signature), [&](ComponentId id) { return id != component; });
	Archetype& target = FindOrCreate(signature);

	from.GetOrAddTransition(component).onRemove = &target;
	target.GetOrAddTransition(component).onAdd  = &from;
	return target;
}

Archetype& ArchetypeGraph::FindOrCreate(std::span<const ComponentId> signature) noexcept
{
	ComponentMask mask;
	for (const ComponentId component : signature)
		mask.set(component);

	if (const auto it = archetypesByMask.find(mask); it != archetypesByMask.end())
		return *it->second;

	ArchetypeUnique owned     = std::make_unique<Archetype>(signature, chunkAllocator);
	Archetype&      archetype = *owned;
	archetypes.push_back(std::move(owned));
	archetypesByMask.emplace(mask, &archetype);
	return archetype;
}
} // namespace ECS_Private
