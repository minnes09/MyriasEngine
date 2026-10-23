#pragma once

#include "ECS/Archetype.h"
#include "ECS/ComponentId.h"

#include <cstddef>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

namespace ECS_Private
{
class ChunkAllocator;

// Owns the archetypes of a World and navigates between them (add/remove a component, with the
// transitions cached on the archetypes). Knows nothing about queries: the World tells the query
// registry about new archetypes by reading Archetypes() (creation order, append only).
// Structural, single-thread: the same rules as World.
class ArchetypeGraph
{
public:
	using ArchetypeUnique = std::unique_ptr<Archetype>;

	// The allocator must outlive the graph (archetypes return their chunks to it on destruction).
	explicit ArchetypeGraph(ChunkAllocator& chunkAllocator) noexcept;
	~ArchetypeGraph() noexcept;
	ArchetypeGraph(const ArchetypeGraph&)            = delete;
	ArchetypeGraph& operator=(const ArchetypeGraph&) = delete;
	ArchetypeGraph(ArchetypeGraph&&)                 = delete;
	ArchetypeGraph& operator=(ArchetypeGraph&&)      = delete;

	// The archetype with no components: every entity starts here.
	[[nodiscard]] Archetype& Empty() const noexcept { return *emptyArchetype; }
	// In creation order; new archetypes are only appended.
	[[nodiscard]] std::span<const ArchetypeUnique> Archetypes() const noexcept { return archetypes; }
	[[nodiscard]] std::size_t                      Count() const noexcept { return archetypes.size(); }

	// Archetype reached by adding / removing a component (created if needed).
	[[nodiscard]] Archetype& AddTarget(Archetype& from, ComponentId component) noexcept;
	[[nodiscard]] Archetype& RemoveTarget(Archetype& from, ComponentId component) noexcept;
	// signature: sorted ComponentIds without duplicates.
	[[nodiscard]] Archetype& FindOrCreate(std::span<const ComponentId> signature) noexcept;

private:
	ChunkAllocator&                               chunkAllocator;
	std::vector<ArchetypeUnique>                  archetypes;
	std::unordered_map<ComponentMask, Archetype*> archetypesByMask; // structural lookups only
	Archetype*                                    emptyArchetype = nullptr;
};
} // namespace ECS_Private
