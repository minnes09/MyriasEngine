#include "ECS/Archetype.h"

#include "Core/Alignment.h"
#include "Core/Assert.h"
#include "Core/Fatal.h"
#include "ECS/Chunk.h"
#include "ECS/ChunkAllocator.h"
#include "ECS/ComponentType.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

namespace Archetype_Private
{
namespace
{
// Moves one component from source to destination and ends the source's lifetime.
void Relocate(const ComponentMeta& meta, std::byte* destination, std::byte* source) noexcept
{
	if (meta.moveConstruct)
	{
		meta.moveConstruct(destination, source, 1);
		meta.destroy(source, 1);
	}
	else
	{
		std::memcpy(destination, source, meta.size);
	}
}

void Destroy(const ComponentMeta& meta, std::byte* component) noexcept
{
	if (meta.destroy)
		meta.destroy(component, 1);
}
} // namespace
} // namespace Archetype_Private

namespace ECS_Private
{
Archetype::Archetype(std::span<const ComponentId> sortedSignature, ChunkAllocator& chunkAllocator) noexcept
    : allocator(chunkAllocator)
    , signature(sortedSignature.begin(), sortedSignature.end())
{
	MYRIAS_ASSERT(std::ranges::adjacent_find(sortedSignature, std::ranges::greater_equal{}) == sortedSignature.end(),
	              "Archetype: signature must be sorted and without duplicates");

	for (const ComponentId component : sortedSignature)
	{
		mask.set(component);
		const ComponentMeta& meta = GetComponentMeta(component);
		if (!meta.bTag)
			columns.push_back({ .component = component, .offset = 0, .meta = &meta });
	}
	ComputeLayout();
}

Archetype::~Archetype() noexcept
{
	for (const ChunkHeader& header : chunks)
	{
		for (std::size_t i = 0; i < columns.size(); ++i)
			if (columns[i].meta->destroy)
				columns[i].meta->destroy(header.chunk->bytes + columns[i].offset, header.count);
		allocator.Free(header.chunk);
	}
	if (spareChunk)
		allocator.Free(spareChunk);
}

int Archetype::ColumnIndex(ComponentId component) const noexcept
{
	const auto it = std::ranges::lower_bound(columns, component, {}, &ArchetypeColumn::component);
	return it != columns.end() && it->component == component ? static_cast<int>(it - columns.begin()) : -1;
}

std::span<const EntityId> Archetype::Entities(std::uint32_t chunkIndex) const noexcept
{
	MYRIAS_ASSERT(chunkIndex < chunks.size(), "Archetype: chunk index out of range");
	return { reinterpret_cast<const EntityId*>(chunks[chunkIndex].chunk->bytes), chunks[chunkIndex].count };
}

void* Archetype::ColumnData(std::uint32_t chunkIndex, int columnIndex) noexcept
{
	return ColumnBytes(chunkIndex, columnIndex);
}

void* Archetype::ComponentData(ArchetypeRow row, int columnIndex) noexcept
{
	MYRIAS_ASSERT(IsValid(row), "Archetype: invalid row");
	return ComponentBytes(row, columnIndex);
}

ArchetypeRow Archetype::AddRow(EntityId entity) noexcept
{
	const ArchetypeRow row = AppendSlot(entity);
	for (int column = 0; column < static_cast<int>(columns.size()); ++column)
		InitializeComponent(row, column);
	return row;
}

EntityId Archetype::RemoveRow(ArchetypeRow row) noexcept
{
	MYRIAS_ASSERT(IsValid(row), "Archetype::RemoveRow: invalid row");
	for (int column = 0; column < static_cast<int>(columns.size()); ++column)
		Archetype_Private::Destroy(*columns[column].meta, ComponentBytes(row, column));
	return FillHoleAndPop(row);
}

ArchetypeMove Archetype::MoveRow(ArchetypeRow row, Archetype& destination) noexcept
{
	return MoveRowImpl(row, destination, std::numeric_limits<ComponentId>::max());
}

ArchetypeMove Archetype::MoveRowUninitialised(ArchetypeRow row, Archetype& destination, ComponentId uninitialised) noexcept
{
	MYRIAS_ASSERT(destination.ColumnIndex(uninitialised) >= 0 && ColumnIndex(uninitialised) < 0,
	              "Archetype::MoveRowUninitialised: component must be a column only in destination");
	return MoveRowImpl(row, destination, uninitialised);
}

ArchetypeMove Archetype::MoveRowImpl(ArchetypeRow row, Archetype& destination, ComponentId uninitialised) noexcept
{
	MYRIAS_ASSERT(&destination != this, "Archetype::MoveRow: destination is the same archetype");
	MYRIAS_ASSERT(IsValid(row), "Archetype::MoveRow: invalid row");

	const EntityId     entity = Entities(row.chunkIndex)[row.row];
	const ArchetypeRow target = destination.AppendSlot(entity);

	// Both column lists are sorted by ComponentId: walk them together. Source-only components are
	// destroyed, shared ones relocated, destination-only ones default-initialised (except `uninitialised`).
	const int sourceCount      = static_cast<int>(columns.size());
	const int destinationCount = static_cast<int>(destination.columns.size());
	int       s                = 0;
	int       d                = 0;
	while (s < sourceCount || d < destinationCount)
	{
		const bool bSourceOnly = d == destinationCount
		                      || (s < sourceCount && columns[s].component < destination.columns[d].component);
		const bool bDestinationOnly = s == sourceCount
		                           || (d < destinationCount && destination.columns[d].component < columns[s].component);
		if (bSourceOnly)
		{
			Archetype_Private::Destroy(*columns[s].meta, ComponentBytes(row, s));
			++s;
		}
		else if (bDestinationOnly)
		{
			if (destination.columns[d].component != uninitialised)
				destination.InitializeComponent(target, d);
			++d;
		}
		else
		{
			Archetype_Private::Relocate(*columns[s].meta, destination.ComponentBytes(target, d), ComponentBytes(row, s));
			++s;
			++d;
		}
	}

	return { .destination = target, .movedEntity = FillHoleAndPop(row) };
}

ArchetypeRow Archetype::CloneRow(ArchetypeRow row, EntityId clone) noexcept
{
	MYRIAS_ASSERT(IsValid(row), "Archetype::CloneRow: invalid row");
	const ArchetypeRow target = AppendSlot(clone); // chunk memory never moves: row stays valid
	for (int column = 0; column < static_cast<int>(columns.size()); ++column)
	{
		const ComponentMeta& meta = *columns[column].meta;
		if (meta.copyConstruct)
			meta.copyConstruct(ComponentBytes(target, column), ComponentBytes(row, column), 1);
		else
			std::memcpy(ComponentBytes(target, column), ComponentBytes(row, column), meta.size);
	}
	return target;
}

ArchetypeTransition* Archetype::FindTransition(ComponentId component) noexcept
{
	const auto it = std::ranges::find(transitions, component, &ArchetypeTransition::component);
	return it != transitions.end() ? &*it : nullptr;
}

ArchetypeTransition& Archetype::GetOrAddTransition(ComponentId component) noexcept
{
	if (ArchetypeTransition* transition = FindTransition(component))
		return *transition;
	return transitions.emplace_back(ArchetypeTransition{ .component = component });
}

void Archetype::ReleaseSpareChunk() noexcept
{
	if (spareChunk)
		allocator.Free(spareChunk);
	spareChunk = nullptr;
}

void Archetype::ComputeLayout() noexcept
{
	std::size_t rowBytes = sizeof(EntityId);
	for (const ArchetypeColumn& column : columns)
	{
		MYRIAS_ASSERT(column.meta->alignment <= kChunkAlignment, "Archetype: component alignment above kChunkAlignment");
		rowBytes += column.meta->size;
	}

	// Lays out `capacity` rows; returns false if they do not fit in a chunk.
	auto layout = [&](std::size_t capacity)
	{
		std::size_t offset = sizeof(EntityId) * capacity;
		for (ArchetypeColumn& column : columns)
		{
			offset        = AlignUp(offset, kChunkAlignment);
			column.offset = static_cast<std::uint32_t>(offset);
			offset += std::size_t{ column.meta->size } * capacity;
		}
		return offset <= kChunkSize;
	};

	std::size_t capacity = std::min<std::size_t>(kChunkSize / rowBytes, std::numeric_limits<std::uint16_t>::max());
	while (capacity > 0 && !layout(capacity))
		--capacity;
	if (capacity == 0) // always checked: AppendSlot would write past the chunk
		FatalError("Archetype: a row of this signature does not fit in a chunk");
	chunkCapacity = static_cast<std::uint16_t>(capacity);
}

ArchetypeRow Archetype::AppendSlot(EntityId entity) noexcept
{
	if (chunks.empty() || chunks.back().count == chunkCapacity)
	{
		Chunk* chunk = spareChunk ? std::exchange(spareChunk, nullptr) : allocator.Allocate();
		chunks.push_back({ .chunk = chunk, .count = 0 });
	}

	ChunkHeader&       header = chunks.back();
	const ArchetypeRow row{ .chunkIndex = static_cast<std::uint32_t>(chunks.size() - 1), .row = header.count };
	std::memcpy(header.chunk->bytes + sizeof(EntityId) * row.row, &entity, sizeof(EntityId));
	++header.count;
	++entityCount;
	return row;
}

void Archetype::InitializeComponent(ArchetypeRow row, int columnIndex) noexcept
{
	const ComponentMeta& meta      = *columns[columnIndex].meta;
	std::byte* const     component = ComponentBytes(row, columnIndex);
	if (meta.defaultConstruct)
		meta.defaultConstruct(component, 1);
	else
		std::memset(component, 0, meta.size); // same result as T{} for trivially constructible types
}

EntityId Archetype::FillHoleAndPop(ArchetypeRow hole) noexcept
{
	// The hole's components are already destroyed or moved out.
	const ArchetypeRow last{ .chunkIndex = static_cast<std::uint32_t>(chunks.size() - 1),
		                     .row        = static_cast<std::uint16_t>(chunks.back().count - 1) };

	EntityId moved;
	if (hole != last)
	{
		for (int column = 0; column < static_cast<int>(columns.size()); ++column)
			Archetype_Private::Relocate(*columns[column].meta, ComponentBytes(hole, column), ComponentBytes(last, column));
		moved = Entities(last.chunkIndex)[last.row];
		std::memcpy(chunks[hole.chunkIndex].chunk->bytes + sizeof(EntityId) * hole.row, &moved, sizeof(EntityId));
	}

	PopLastSlot();
	return moved;
}

void Archetype::PopLastSlot() noexcept
{
	// The last row's components are already destroyed or moved out.
	ChunkHeader& tail = chunks.back();
	--tail.count;
	--entityCount;
	if (tail.count == 0)
	{
		if (spareChunk)
			allocator.Free(spareChunk);
		spareChunk = tail.chunk;
		chunks.pop_back();
	}
}

std::byte* Archetype::ColumnBytes(std::uint32_t chunkIndex, int columnIndex) noexcept
{
	MYRIAS_ASSERT(chunkIndex < chunks.size(), "Archetype: chunk index out of range");
	MYRIAS_ASSERT(columnIndex >= 0 && columnIndex < static_cast<int>(columns.size()), "Archetype: column out of range");
	return chunks[chunkIndex].chunk->bytes + columns[columnIndex].offset;
}

std::byte* Archetype::ComponentBytes(ArchetypeRow row, int columnIndex) noexcept
{
	return ColumnBytes(row.chunkIndex, columnIndex) + std::size_t{ columns[columnIndex].meta->size } * row.row;
}

bool Archetype::IsValid(ArchetypeRow row) const noexcept
{
	return row.chunkIndex < chunks.size() && row.row < chunks[row.chunkIndex].count;
}
} // namespace ECS_Private
