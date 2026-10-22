#include "ECS/Archetype.h"

#include "ECS/ChunkAllocator.h"
#include "Support/AllocationCounter.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace
{
struct Position
{
	float x;
	float y;
};

struct Velocity
{
	float dx;
	float dy;
};

struct Health
{
	int value;
};

struct Enemy
{
};

struct WithDefaults
{
	int characterSize = 16;
};

struct alignas(16) Aligned16
{
	float values[4];
};

struct Big
{
	std::byte data[4000];
};

struct BigA
{
	std::byte data[9000];
};

struct BigB
{
	std::byte data[9000];
};

struct Tracked // cold: counts live instances to detect leaks
{
	static inline int alive = 0;

	std::string text = "tracked";

	Tracked() { ++alive; }
	Tracked(const Tracked& other) : text(other.text) { ++alive; }
	Tracked(Tracked&& other) noexcept : text(std::move(other.text)) { ++alive; }
	Tracked& operator=(const Tracked&) = default;
	Tracked& operator=(Tracked&&)      = default;
	~Tracked() { --alive; }
};

template <typename... Ts>
std::vector<ComponentId> SignatureOf()
{
	std::vector<ComponentId> signature{ GetComponentId<Ts>()... };
	std::ranges::sort(signature);
	return signature;
}

EntityId Entity(std::uint32_t index)
{
	return EntityId::Create(index, 1, 1);
}

template <typename T>
T& ComponentAt(Archetype& archetype, ArchetypeRow row)
{
	return *static_cast<T*>(archetype.ComponentData(row, archetype.ColumnIndex(GetComponentId<T>())));
}
} // namespace

template <>
inline constexpr bool IsColdComponent<Tracked> = true; // test type: exercises the non-trivial paths

TEST(Archetype, LayoutMatchesTheDocumentedExample)
{
	ChunkAllocator allocator;
	Archetype      archetype(SignatureOf<Position, Velocity, Health, Enemy>(), allocator);

	// 8 + 8 + 8 + 4 bytes per row, every column 64-aligned: 584 rows (see ADR-0004 layout diagram).
	EXPECT_EQ(archetype.ChunkCapacity(), 584);
	ASSERT_EQ(archetype.Columns().size(), 3u); // the tag has no column
	EXPECT_EQ(archetype.Signature().size(), 4u);

	std::uint32_t previousEnd = sizeof(EntityId) * archetype.ChunkCapacity();
	for (const ArchetypeColumn& column : archetype.Columns())
	{
		EXPECT_EQ(column.offset % kChunkAlignment, 0u);
		EXPECT_GE(column.offset, previousEnd);
		previousEnd = column.offset + column.meta->size * archetype.ChunkCapacity();
	}
	EXPECT_LE(previousEnd, kChunkSize);
}

TEST(Archetype, CapacityIsMaximalWithMixedAlignment)
{
	ChunkAllocator allocator;
	Archetype      archetype(SignatureOf<Position, Aligned16, Health>(), allocator);

	auto endFor = [&](std::size_t capacity)
	{
		std::size_t offset = sizeof(EntityId) * capacity;
		for (const ArchetypeColumn& column : archetype.Columns())
			offset = (offset + kChunkAlignment - 1) / kChunkAlignment * kChunkAlignment + column.meta->size * capacity;
		return offset;
	};
	EXPECT_LE(endFor(archetype.ChunkCapacity()), kChunkSize);
	EXPECT_GT(endFor(archetype.ChunkCapacity() + 1u), kChunkSize);
}

TEST(Archetype, TagHasNoColumn)
{
	ChunkAllocator allocator;
	Archetype      archetype(SignatureOf<Position, Enemy>(), allocator);
	EXPECT_EQ(archetype.ColumnIndex(GetComponentId<Enemy>()), -1);
	EXPECT_GE(archetype.ColumnIndex(GetComponentId<Position>()), 0);
	EXPECT_TRUE(archetype.Mask().test(GetComponentId<Enemy>()));
}

TEST(Archetype, RowsFillChunksInOrder)
{
	ChunkAllocator allocator;
	Archetype      archetype(SignatureOf<Big>(), allocator);
	ASSERT_EQ(archetype.ChunkCapacity(), 4);

	for (std::uint32_t i = 0; i < 5; ++i)
		static_cast<void>(archetype.AddRow(Entity(i)));

	ASSERT_EQ(archetype.Chunks().size(), 2u);
	EXPECT_EQ(archetype.Chunks()[0].count, 4);
	EXPECT_EQ(archetype.Chunks()[1].count, 1);
	EXPECT_EQ(archetype.EntityCount(), 5u);
	EXPECT_EQ(archetype.Entities(1)[0], Entity(4));
}

TEST(Archetype, NewRowsAreDefaultInitialised)
{
	ChunkAllocator allocator;
	Archetype      archetype(SignatureOf<Position, WithDefaults>(), allocator);
	const ArchetypeRow row = archetype.AddRow(Entity(0));

	EXPECT_EQ(ComponentAt<Position>(archetype, row).x, 0.f);
	EXPECT_EQ(ComponentAt<Position>(archetype, row).y, 0.f);
	EXPECT_EQ(ComponentAt<WithDefaults>(archetype, row).characterSize, 16);
}

TEST(Archetype, RemoveFillsTheHoleWithTheLastRow)
{
	ChunkAllocator allocator;
	Archetype      archetype(SignatureOf<Health>(), allocator);
	std::vector<ArchetypeRow> rows;
	for (std::uint32_t i = 0; i < 3; ++i)
	{
		rows.push_back(archetype.AddRow(Entity(i)));
		ComponentAt<Health>(archetype, rows.back()).value = static_cast<int>(i) * 10;
	}

	const EntityId moved = archetype.RemoveRow(rows[0]);
	EXPECT_EQ(moved, Entity(2));
	EXPECT_EQ(archetype.EntityCount(), 2u);
	EXPECT_EQ(archetype.Entities(0)[0], Entity(2));
	EXPECT_EQ(ComponentAt<Health>(archetype, rows[0]).value, 20);

	// Removing the last row moves nothing.
	EXPECT_TRUE(archetype.RemoveRow(rows[1]).IsNull());
	EXPECT_EQ(archetype.EntityCount(), 1u);
}

TEST(Archetype, RemoveAcrossChunksKeepsChunksPacked)
{
	ChunkAllocator allocator;
	Archetype      archetype(SignatureOf<Big>(), allocator);
	for (std::uint32_t i = 0; i < 5; ++i)
		static_cast<void>(archetype.AddRow(Entity(i)));

	// The only row of chunk 1 fills the hole in chunk 0, and chunk 1 disappears.
	EXPECT_EQ(archetype.RemoveRow({ .chunkIndex = 0, .row = 1 }), Entity(4));
	ASSERT_EQ(archetype.Chunks().size(), 1u);
	EXPECT_EQ(archetype.Entities(0)[1], Entity(4));
}

TEST(Archetype, MoveRowKeepsSharedComponents)
{
	ChunkAllocator allocator;
	Archetype      source(SignatureOf<Position, Health>(), allocator);
	Archetype      destination(SignatureOf<Position, WithDefaults>(), allocator);

	const ArchetypeRow row                 = source.AddRow(Entity(7));
	ComponentAt<Position>(source, row)     = { 1.f, 2.f };
	ComponentAt<Health>(source, row).value = 99;

	const ArchetypeMove move = source.MoveRow(row, destination);
	EXPECT_TRUE(move.movedEntity.IsNull());
	EXPECT_EQ(source.EntityCount(), 0u);
	EXPECT_EQ(destination.Entities(move.destination.chunkIndex)[move.destination.row], Entity(7));
	EXPECT_EQ(ComponentAt<Position>(destination, move.destination).x, 1.f);
	EXPECT_EQ(ComponentAt<Position>(destination, move.destination).y, 2.f);
	EXPECT_EQ(ComponentAt<WithDefaults>(destination, move.destination).characterSize, 16);
}

TEST(Archetype, MoveRowHandlesSourceOnlySharedAndDestinationOnlyColumns)
{
	const int aliveBefore = Tracked::alive;
	{
		ChunkAllocator allocator;

		// Shared cold column moved, source-only trivial column and tag dropped, destination-only
		// default-constructed column (value 16) and zeroed trivial column. The columns interleave
		// differently depending on the registration order of the ids: the checks do not depend on it.
		{
			Archetype source(SignatureOf<Tracked, Health, Enemy>(), allocator);
			Archetype destination(SignatureOf<Position, Tracked, WithDefaults>(), allocator);
			const ArchetypeRow first                  = source.AddRow(Entity(1));
			const ArchetypeRow second                 = source.AddRow(Entity(2));
			ComponentAt<Tracked>(source, first).text  = "one";
			ComponentAt<Tracked>(source, second).text = "two";
			ComponentAt<Health>(source, first).value  = 5;
			ASSERT_EQ(Tracked::alive, aliveBefore + 2);

			const ArchetypeMove move = source.MoveRow(first, destination);
			EXPECT_EQ(move.movedEntity, Entity(2)); // the last row filled the hole
			EXPECT_EQ(source.EntityCount(), 1u);
			EXPECT_EQ(ComponentAt<Tracked>(source, first).text, "two");
			EXPECT_EQ(destination.Entities(0)[move.destination.row], Entity(1));
			EXPECT_EQ(ComponentAt<Tracked>(destination, move.destination).text, "one");
			EXPECT_EQ(ComponentAt<Position>(destination, move.destination).x, 0.f);
			EXPECT_EQ(ComponentAt<WithDefaults>(destination, move.destination).characterSize, 16);
			EXPECT_EQ(Tracked::alive, aliveBefore + 2); // moved, not copied or leaked
		}
		EXPECT_EQ(Tracked::alive, aliveBefore);

		// Cold column only in the source: destroyed by the move.
		{
			Archetype source(SignatureOf<Tracked, Health>(), allocator);
			Archetype destination(SignatureOf<Position, WithDefaults>(), allocator);
			const ArchetypeRow row = source.AddRow(Entity(3));
			ASSERT_EQ(Tracked::alive, aliveBefore + 1);
			static_cast<void>(source.MoveRow(row, destination));
			EXPECT_EQ(Tracked::alive, aliveBefore);
		}

		// Cold column only in the destination: default-constructed by the move.
		{
			Archetype source(SignatureOf<Position, Health>(), allocator);
			Archetype destination(SignatureOf<Tracked, WithDefaults, Position, Health>(), allocator);
			const ArchetypeRow row               = source.AddRow(Entity(4));
			ComponentAt<Position>(source, row).x = 3.f;
			const ArchetypeMove move             = source.MoveRow(row, destination);
			EXPECT_EQ(ComponentAt<Tracked>(destination, move.destination).text, "tracked");
			EXPECT_EQ(ComponentAt<Position>(destination, move.destination).x, 3.f);
			EXPECT_EQ(Tracked::alive, aliveBefore + 1);
		}
		EXPECT_EQ(Tracked::alive, aliveBefore);

		// MoveRowUninitialised: the `uninitialised` column is left raw (the caller constructs it); the
		// others are initialised, whatever their position around it.
		{
			Archetype source(SignatureOf<Position>(), allocator);
			Archetype destination(SignatureOf<Position, WithDefaults, Tracked, Health>(), allocator);
			const ArchetypeRow  row  = source.AddRow(Entity(5));
			const ArchetypeMove move = source.MoveRowUninitialised(row, destination, GetComponentId<WithDefaults>());
			std::construct_at(&ComponentAt<WithDefaults>(destination, move.destination), WithDefaults{ 5 });
			EXPECT_EQ(ComponentAt<WithDefaults>(destination, move.destination).characterSize, 5);
			EXPECT_EQ(ComponentAt<Tracked>(destination, move.destination).text, "tracked");
			EXPECT_EQ(ComponentAt<Health>(destination, move.destination).value, 0);
		}
	}
	EXPECT_EQ(Tracked::alive, aliveBefore);
}

TEST(Archetype, CloneRowIsIndependent)
{
	ChunkAllocator allocator;
	Archetype      archetype(SignatureOf<Health>(), allocator);
	const ArchetypeRow original                    = archetype.AddRow(Entity(0));
	ComponentAt<Health>(archetype, original).value = 5;

	const ArchetypeRow clone                    = archetype.CloneRow(original, Entity(1));
	ComponentAt<Health>(archetype, clone).value = 6;

	EXPECT_EQ(ComponentAt<Health>(archetype, original).value, 5);
	EXPECT_EQ(archetype.Entities(clone.chunkIndex)[clone.row], Entity(1));
}

TEST(Archetype, ColdComponentsDoNotLeak)
{
	const int aliveBefore = Tracked::alive;
	{
		ChunkAllocator allocator;
		Archetype      withTracked(SignatureOf<Tracked, Health>(), allocator);
		Archetype      withoutTracked(SignatureOf<Health>(), allocator);

		std::vector<ArchetypeRow> rows;
		for (std::uint32_t i = 0; i < 6; ++i)
			rows.push_back(withTracked.AddRow(Entity(i)));
		ComponentAt<Tracked>(withTracked, rows[5]).text = "last";

		static_cast<void>(withTracked.RemoveRow(rows[0]));               // last row relocated into the hole
		EXPECT_EQ(ComponentAt<Tracked>(withTracked, rows[0]).text, "last");
		static_cast<void>(withTracked.CloneRow(rows[0], Entity(10)));                     // copy
		static_cast<void>(withTracked.MoveRow(rows[1], withoutTracked)); // Tracked destroyed on the way out
		EXPECT_EQ(Tracked::alive, aliveBefore + 5);
	} // archetype destructors destroy the remaining rows
	EXPECT_EQ(Tracked::alive, aliveBefore);
}

TEST(Archetype, MoveRowFromANonLastChunkFillsTheHoleAndKeepsColdData)
{
	const int aliveBefore = Tracked::alive;
	{
		ChunkAllocator allocator;
		Archetype      source(SignatureOf<Big, Tracked, Health>(), allocator);
		Archetype      destination(SignatureOf<Tracked, Health>(), allocator);
		const auto     capacity = static_cast<std::uint32_t>(source.ChunkCapacity());
		ASSERT_GE(capacity, 2u);

		// capacity + 1 rows: chunk 0 is full, chunk 1 holds only the last entity.
		for (std::uint32_t i = 0; i <= capacity; ++i)
		{
			const ArchetypeRow row                 = source.AddRow(Entity(i));
			ComponentAt<Tracked>(source, row).text = "t" + std::to_string(i);
			ComponentAt<Health>(source, row).value = static_cast<int>(i);
		}
		ASSERT_EQ(source.Chunks().size(), 2u);

		const ArchetypeRow  hole = { .chunkIndex = 0, .row = 0 };
		const ArchetypeMove move = source.MoveRow(hole, destination);

		// The last row (chunk 1) moved into the hole of chunk 0, and chunk 1 disappeared.
		EXPECT_EQ(move.movedEntity, Entity(capacity));
		ASSERT_EQ(source.Chunks().size(), 1u);
		EXPECT_EQ(source.EntityCount(), capacity);
		EXPECT_EQ(source.Entities(0)[0], Entity(capacity));
		EXPECT_EQ(ComponentAt<Tracked>(source, hole).text, "t" + std::to_string(capacity));
		EXPECT_EQ(ComponentAt<Health>(source, hole).value, static_cast<int>(capacity));

		EXPECT_EQ(destination.Entities(move.destination.chunkIndex)[move.destination.row], Entity(0));
		EXPECT_EQ(ComponentAt<Tracked>(destination, move.destination).text, "t0");
		EXPECT_EQ(ComponentAt<Health>(destination, move.destination).value, 0);

		// One Tracked per row, none leaked by the move or the relocation.
		EXPECT_EQ(Tracked::alive, aliveBefore + static_cast<int>(capacity) + 1);
	}
	EXPECT_EQ(Tracked::alive, aliveBefore);
}

TEST(Archetype, SpareChunkIsReusedByTheNextAdd)
{
	ChunkAllocator allocator;
	Archetype      archetype(SignatureOf<Big>(), allocator);
	const auto     capacity = static_cast<std::uint32_t>(archetype.ChunkCapacity());
	for (std::uint32_t i = 0; i <= capacity; ++i) // a full chunk plus one row: opens the second chunk
		static_cast<void>(archetype.AddRow(Entity(i)));
	ASSERT_EQ(archetype.Chunks().size(), 2u);
	const Chunk* secondChunk = archetype.Chunks()[1].chunk;

	static_cast<void>(archetype.RemoveRow({ .chunkIndex = 1, .row = 0 })); // the second chunk becomes the spare
	ASSERT_EQ(archetype.Chunks().size(), 1u);
	const std::size_t allocated = allocator.AllocatedChunkCount();

	static_cast<void>(archetype.AddRow(Entity(capacity)));
	EXPECT_EQ(archetype.Chunks().size(), 2u);
	EXPECT_EQ(archetype.Chunks()[1].chunk, secondChunk);
	EXPECT_EQ(allocator.AllocatedChunkCount(), allocated);
}

TEST(ArchetypeDeathTest, SignatureWhoseRowDoesNotFitInAChunkAborts)
{
	// Each component fits in a chunk on its own (GetComponentId static_assert), together they do not.
	EXPECT_DEATH(
	    {
		    ChunkAllocator allocator;
		    Archetype      archetype(SignatureOf<BigA, BigB>(), allocator);
	    },
	    "does not fit in a chunk");
}

TEST(Archetype, KeepsOneSpareChunkAndReturnsAllOnDestruction)
{
	ChunkAllocator allocator;
	{
		Archetype archetype(SignatureOf<Big>(), allocator);
		for (std::uint32_t i = 0; i < 8; ++i)
			static_cast<void>(archetype.AddRow(Entity(i)));
		EXPECT_EQ(allocator.AllocatedChunkCount(), 2u);

		while (archetype.EntityCount() > 0)
			static_cast<void>(archetype.RemoveRow({ .chunkIndex = 0, .row = 0 }));
		EXPECT_TRUE(archetype.Chunks().empty());
		EXPECT_EQ(allocator.AllocatedChunkCount(), 1u); // the spare
	}
	EXPECT_EQ(allocator.AllocatedChunkCount(), 0u);
}

TEST(Archetype, SteadyStateAddRemoveDoesNotAllocate)
{
	ChunkAllocator allocator;
	Archetype      archetype(SignatureOf<Position, Velocity>(), allocator);
	const std::uint32_t rowCount = archetype.ChunkCapacity() * 2u + 10u; // spans three chunks

	auto cycle = [&]
	{
		for (std::uint32_t i = 0; i < rowCount; ++i)
			static_cast<void>(archetype.AddRow(Entity(i)));
		while (archetype.EntityCount() > 0)
			static_cast<void>(archetype.RemoveRow({ .chunkIndex = 0, .row = 0 }));
	};
	cycle(); // warm-up: chunks, header vector

	std::size_t count = 0;
	{
		const AllocationScope scope;
		cycle();
		count = scope.Count();
	}
	EXPECT_EQ(count, 0u);
}
