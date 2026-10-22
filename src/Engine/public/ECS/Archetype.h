#pragma once

#include "ECS/Chunk.h"
#include "ECS/ComponentType.h"
#include "ECS/EntityId.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

class Archetype;
class ChunkAllocator;

// One per non-tag component: where its column starts inside every chunk of the archetype.
struct ArchetypeColumn
{
	ComponentId          component = 0;
	std::uint32_t        offset    = 0;
	const ComponentMeta* meta      = nullptr;
};

// Per-chunk bookkeeping, kept by the archetype outside the chunk's data.
struct ChunkHeader
{
	Chunk*        chunk = nullptr;
	std::uint16_t count = 0;
};

// Where an entity's components live inside an archetype.
struct ArchetypeRow
{
	std::uint32_t chunkIndex = 0;
	std::uint16_t row        = 0;

	friend constexpr bool operator==(ArchetypeRow, ArchetypeRow) noexcept = default;
};

// Cached edge of the archetype graph: the archetype reached by adding/removing one component.
struct ArchetypeTransition
{
	ComponentId component = 0;
	Archetype*  onAdd     = nullptr;
	Archetype*  onRemove  = nullptr;
};

// Result of moving a row to another archetype.
struct ArchetypeMove
{
	ArchetypeRow destination;  // the entity's new row
	EntityId     movedEntity;  // entity moved into the vacated source row, null if none
};

// Stores the entities that have exactly one set of components (the signature), in chunks.
// Chunk layout: the EntityId column, then one SoA column per non-tag component in ComponentId
// order, each starting on a kChunkAlignment boundary. Every chunk is full except the last one,
// so iteration never meets holes.
//
// Structural operations (AddRow, RemoveRow, MoveRow, CloneRow) run on structural paths only,
// never per entity inside a query.
class Archetype
{
public:
	// signature: sorted ComponentIds without duplicates (tags included).
	Archetype(std::span<const ComponentId> sortedSignature, ChunkAllocator& chunkAllocator) noexcept;
	~Archetype() noexcept;
	Archetype(const Archetype&)            = delete;
	Archetype& operator=(const Archetype&) = delete;
	Archetype(Archetype&&)                 = delete;
	Archetype& operator=(Archetype&&)      = delete;

	[[nodiscard]] const ComponentMask&             Mask() const noexcept { return mask; }
	[[nodiscard]] std::span<const ComponentId>     Signature() const noexcept { return signature; }
	[[nodiscard]] std::span<const ArchetypeColumn> Columns() const noexcept { return columns; }
	// Index into Columns(), or -1 if the component is absent or is a tag.
	[[nodiscard]] int                              ColumnIndex(ComponentId component) const noexcept;

	[[nodiscard]] std::uint16_t                ChunkCapacity() const noexcept { return chunkCapacity; }
	[[nodiscard]] std::span<const ChunkHeader> Chunks() const noexcept { return chunks; }
	[[nodiscard]] std::size_t                  EntityCount() const noexcept { return entityCount; }

	[[nodiscard]] std::span<const EntityId> Entities(std::uint32_t chunkIndex) const noexcept;
	[[nodiscard]] void*               ColumnData(std::uint32_t chunkIndex, int columnIndex) noexcept;
	[[nodiscard]] void*               ComponentData(ArchetypeRow row, int columnIndex) noexcept;

	// Appends a row for entity; components are default-initialised (zeroed if trivially constructible).
	[[nodiscard]] ArchetypeRow AddRow(EntityId entity) noexcept;
	// Destroys the row's components and fills the hole with the last row.
	// Returns the entity moved into the hole (its row changed), or null if none moved.
	[[nodiscard]] EntityId RemoveRow(ArchetypeRow row) noexcept;
	// Moves the row into destination: shared components are moved, components only in this
	// archetype are destroyed, components only in destination are default-initialised.
	[[nodiscard]] ArchetypeMove MoveRow(ArchetypeRow row, Archetype& destination) noexcept;
	// Same as MoveRow, but the column of `uninitialised` (present only in destination) is left as
	// raw memory: the caller must construct a component there before anything else.
	[[nodiscard]] ArchetypeMove MoveRowUninitialised(ArchetypeRow row, Archetype& destination, ComponentId uninitialised) noexcept;
	// Appends a copy of row for entity clone.
	[[nodiscard]] ArchetypeRow CloneRow(ArchetypeRow row, EntityId clone) noexcept;

	// Graph edges, filled by the World on first use (few per archetype: linear scan, no hashing).
	[[nodiscard]] ArchetypeTransition* FindTransition(ComponentId component) noexcept;
	ArchetypeTransition&               GetOrAddTransition(ComponentId component) noexcept;

	// Returns the spare chunk to the allocator (maintenance, e.g. World::Trim).
	void ReleaseSpareChunk() noexcept;

private:
	void                     ComputeLayout() noexcept;
	[[nodiscard]] ArchetypeRow AppendSlot(EntityId entity) noexcept;
	void                     InitializeComponent(ArchetypeRow row, int columnIndex) noexcept;
	[[nodiscard]] ArchetypeMove MoveRowImpl(ArchetypeRow row, Archetype& destination, ComponentId uninitialised) noexcept;
	void                     PopLastSlot() noexcept;
	[[nodiscard]] EntityId   FillHoleAndPop(ArchetypeRow hole) noexcept;
	[[nodiscard]] std::byte* ColumnBytes(std::uint32_t chunkIndex, int columnIndex) noexcept;
	[[nodiscard]] std::byte* ComponentBytes(ArchetypeRow row, int columnIndex) noexcept;
	[[nodiscard]] bool       IsValid(ArchetypeRow row) const noexcept;

	ChunkAllocator&              allocator;
	ComponentMask                mask;
	std::vector<ComponentId>     signature;
	std::vector<ArchetypeColumn> columns;
	std::vector<ChunkHeader>         chunks;           // non-empty chunks only
	std::vector<ArchetypeTransition> transitions;
	Chunk*                       spareChunk    = nullptr; // last emptied chunk, kept to avoid alloc/free thrash
	std::uint16_t                chunkCapacity = 0;
	std::size_t                  entityCount   = 0;
};
