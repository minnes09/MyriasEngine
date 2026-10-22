#include "ECS/World.h"

#include "Support/AllocationCounter.h"

#include <gtest/gtest.h>

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
} // namespace

template <>
inline constexpr bool IsColdComponent<Tracked> = true; // test type: exercises the non-trivial paths

namespace
{
std::size_t ColdChunkCount(World& world)
{
	return world.Query<Health, Tracked>().ChunkCount(); // helper: commas inside macro arguments
}
} // namespace

TEST(World, CreatedEntitiesAreDistinctAndAlive)
{
	World          world;
	const EntityId a = world.CreateEntity();
	const EntityId b = world.CreateEntity();
	EXPECT_NE(a, b);
	EXPECT_TRUE(world.IsAlive(a));
	EXPECT_EQ(a.GetWorldId(), world.GetWorldId());
	EXPECT_EQ(world.EntityCount(), 2u);
}

TEST(World, AddedComponentIsRetrievable)
{
	World          world;
	const EntityId entity = world.CreateEntity();
	world.Add(entity, Position{ 1.f, 2.f });

	ASSERT_NE(world.TryGet<Position>(entity), nullptr);
	EXPECT_EQ(world.Get<Position>(entity).x, 1.f);
	EXPECT_TRUE(world.Has<Position>(entity));
	EXPECT_FALSE(world.Has<Velocity>(entity));
	EXPECT_EQ(world.TryGet<Velocity>(entity), nullptr);
}

TEST(World, AddAndRemoveKeepOtherComponents)
{
	World          world;
	const EntityId entity = world.CreateEntity();
	world.Add(entity, Position{ 1.f, 2.f });
	world.Add(entity, Velocity{ 3.f, 4.f });
	world.Add(entity, Health{ 50 });

	world.Remove<Velocity>(entity);
	EXPECT_FALSE(world.Has<Velocity>(entity));
	EXPECT_EQ(world.Get<Position>(entity).y, 2.f);
	EXPECT_EQ(world.Get<Health>(entity).value, 50);

	world.Remove<Velocity>(entity); // absent: no-op
	EXPECT_EQ(world.Get<Health>(entity).value, 50);
}

TEST(World, RepeatedTransitionsReuseArchetypes)
{
	World          world;
	const EntityId entity = world.CreateEntity();
	world.Add(entity, Position{});
	world.Remove<Position>(entity);
	const std::size_t archetypeCount = world.ArchetypeCount();

	for (int i = 0; i < 5; ++i)
	{
		world.Add(entity, Position{});
		world.Remove<Position>(entity);
	}
	EXPECT_EQ(world.ArchetypeCount(), archetypeCount);
}

TEST(World, DestroyRemovesTheEntityAndKeepsOthersIntact)
{
	World                 world;
	std::vector<EntityId> entities;
	for (int i = 0; i < 4; ++i)
	{
		entities.push_back(world.CreateEntity());
		world.Add(entities.back(), Health{ i * 10 });
	}

	world.DestroyEntity(entities[1]); // the last row moves into its place
	EXPECT_FALSE(world.IsAlive(entities[1]));
	EXPECT_EQ(world.TryGet<Health>(entities[1]), nullptr);
	EXPECT_EQ(world.Get<Health>(entities[0]).value, 0);
	EXPECT_EQ(world.Get<Health>(entities[2]).value, 20);
	EXPECT_EQ(world.Get<Health>(entities[3]).value, 30);
}

TEST(World, StaleIdIsIgnored)
{
	World          world;
	const EntityId entity = world.CreateEntity();
	world.DestroyEntity(entity);

	world.DestroyEntity(entity); // no-op
	world.Remove<Position>(entity);
	EXPECT_FALSE(world.IsAlive(entity));
	EXPECT_FALSE(world.Has<Position>(entity));
	EXPECT_EQ(world.TryGet<Position>(entity), nullptr);
}

TEST(World, TagsAreTrackedWithoutStorage)
{
	World          world;
	const EntityId entity = world.CreateEntity();
	world.Add<Enemy>(entity);
	EXPECT_TRUE(world.Has<Enemy>(entity));
	EXPECT_NE(world.TryGet<Enemy>(entity), nullptr);

	world.Remove<Enemy>(entity);
	EXPECT_FALSE(world.Has<Enemy>(entity));
}

TEST(World, DestroyAllWithOnlyTouchesMatchingEntities)
{
	World          world;
	const EntityId enemy   = world.CreateEntity();
	const EntityId friendly = world.CreateEntity();
	world.Add<Enemy>(enemy);
	world.Add(enemy, Health{ 1 });
	world.Add(friendly, Health{ 2 });

	world.DestroyAllWith<Enemy>();
	EXPECT_FALSE(world.IsAlive(enemy));
	ASSERT_TRUE(world.IsAlive(friendly));
	EXPECT_EQ(world.Get<Health>(friendly).value, 2);
}

TEST(World, CloneIsIndependent)
{
	World          world;
	const EntityId source = world.CreateEntity();
	world.Add(source, Health{ 7 });

	const EntityId clone = world.CloneEntity(source);
	EXPECT_NE(clone, source);
	EXPECT_EQ(world.Get<Health>(clone).value, 7);

	world.Get<Health>(clone).value = 3;
	EXPECT_EQ(world.Get<Health>(source).value, 7);
}

TEST(World, ClearDestroysEverything)
{
	World                 world;
	std::vector<EntityId> entities;
	for (int i = 0; i < 10; ++i)
	{
		entities.push_back(world.CreateEntity());
		if (i % 2 == 0)
			world.Add(entities.back(), Position{});
	}

	world.Clear();
	EXPECT_EQ(world.EntityCount(), 0u);
	for (const EntityId entity : entities)
		EXPECT_FALSE(world.IsAlive(entity));
}

TEST(World, ColdComponentsDoNotLeak)
{
	const int aliveBefore = Tracked::alive;
	{
		World          world;
		const EntityId a = world.CreateEntity();
		world.Add(a, Tracked{});
		world.Add(a, Health{ 1 });             // archetype move with a cold column
		const EntityId b = world.CloneEntity(a);
		world.Remove<Tracked>(b);
		world.DestroyEntity(world.CloneEntity(a));
		EXPECT_EQ(Tracked::alive, aliveBefore + 1);
	} // the World's destruction destroys the remaining Tracked
	EXPECT_EQ(Tracked::alive, aliveBefore);
}

TEST(WorldDeathTest, AddingAPresentComponentAsserts)
{
	World          world;
	const EntityId entity = world.CreateEntity();
	world.Add(entity, Position{});
	EXPECT_DEBUG_DEATH(world.Add(entity, Position{}), "component already present");
}

TEST(World, AddingAPresentComponentReplacesTheValueWhenAssertsAreOff)
{
	if constexpr (MYRIAS_ASSERTS_ENABLED)
		GTEST_SKIP() << "with asserts enabled this is a bug report (see the death test above)";
	else
	{
		const int aliveBefore = Tracked::alive;
		{
			World          world;
			const EntityId entity = world.CreateEntity();
			world.Add(entity, Position{ 1.f, 2.f });
			world.Add(entity, Position{ 3.f, 4.f });
			EXPECT_EQ(world.Get<Position>(entity).x, 3.f);
			EXPECT_EQ(world.Get<Position>(entity).y, 4.f);

			world.Add(entity, Tracked{});
			Tracked replacement;
			replacement.text = "replaced";
			world.Add(entity, std::move(replacement));
			EXPECT_EQ(world.Get<Tracked>(entity).text, "replaced");
			EXPECT_EQ(Tracked::alive, aliveBefore + 2); // the stored value and the moved-from local: no leak, no double destroy
		}
		EXPECT_EQ(Tracked::alive, aliveBefore);
	}
}

TEST(WorldDeathTest, AddOnAnIdWhoseSlotWasReusedAborts)
{
	World          world;
	const EntityId stale = world.CreateEntity();
	world.DestroyEntity(stale);
	const EntityId reused = world.CreateEntity();
	ASSERT_EQ(reused.Index(), stale.Index());
	ASSERT_NE(reused, stale);
	EXPECT_DEATH(world.Add(stale, Health{ 1 }), "World::Add: entity is not alive");
	EXPECT_DEATH(world.Add<Enemy>(EntityId{}), "World::Add: entity is not alive");
}

TEST(WorldDeathTest, CloneOfAnIdWhoseSlotWasReusedAborts)
{
	World          world;
	const EntityId stale = world.CreateEntity();
	world.DestroyEntity(stale);
	const EntityId reused = world.CreateEntity();
	ASSERT_EQ(reused.Index(), stale.Index());
	EXPECT_DEATH(static_cast<void>(world.CloneEntity(stale)), "World::CloneEntity: entity is not alive");
	EXPECT_DEATH(static_cast<void>(world.CloneEntity(EntityId{})), "World::CloneEntity: entity is not alive");
}

TEST(World, SteadyStateStructuralChangesDoNotAllocate)
{
	World                 world;
	std::vector<EntityId> entities;
	entities.reserve(256);

	auto cycle = [&]
	{
		for (int i = 0; i < 256; ++i)
		{
			const EntityId entity = world.CreateEntity();
			world.Add(entity, Position{});
			world.Add(entity, Velocity{});
			world.Remove<Position>(entity);
			entities.push_back(entity);
		}
		for (const EntityId entity : entities)
			world.DestroyEntity(entity);
		entities.clear();
	};
	cycle(); // warm-up: archetypes, transitions, chunks, entity index pages

	std::size_t count = 0;
	{
		const AllocationScope scope;
		cycle();
		count = scope.Count();
	}
	EXPECT_EQ(count, 0u);
}

TEST(World, AddMovesTheLastRowIntoTheHole)
{
	World          world;
	const EntityId a = world.CreateEntity();
	const EntityId b = world.CreateEntity();
	const EntityId c = world.CreateEntity();
	world.Add(a, Health{ 10 });
	world.Add(b, Health{ 20 });
	world.Add(c, Health{ 30 });

	world.Add(a, Velocity{ 1.f, 2.f }); // a is not the last row: c is moved into its hole
	EXPECT_EQ(world.Get<Health>(a).value, 10);
	EXPECT_EQ(world.Get<Velocity>(a).dy, 2.f);
	EXPECT_EQ(world.Get<Health>(b).value, 20);
	EXPECT_EQ(world.Get<Health>(c).value, 30);

	world.Remove<Health>(c);
	EXPECT_FALSE(world.Has<Health>(c));
	EXPECT_EQ(world.Get<Health>(b).value, 20);
	EXPECT_EQ(world.Get<Health>(a).value, 10);

	world.DestroyEntity(b);
	EXPECT_FALSE(world.IsAlive(b));
	EXPECT_TRUE(world.IsAlive(c));
	EXPECT_EQ(world.Get<Health>(a).value, 10);
	EXPECT_EQ(world.Get<Velocity>(a).dx, 1.f);
	EXPECT_EQ(world.EntityCount(), 2u);

	world.DestroyEntity(c);
	EXPECT_EQ(world.Get<Health>(a).value, 10);
	EXPECT_EQ(world.EntityCount(), 1u);
}

TEST(World, ReserveReducesCreateAllocations)
{
	// A World-level check cannot be allocation-free: CreateEntity also places the row in the empty
	// archetype, which opens a chunk (block + chunk list). The exact zero is checked on the
	// EntityIndex; here the reserved World must allocate strictly less than a control World.
	constexpr int kCount = 10000;
	World         reserved;
	World         control;
	reserved.Reserve(kCount);

	std::size_t reservedCount = 0;
	std::size_t controlCount  = 0;
	{
		const AllocationScope scope;
		for (int i = 0; i < kCount; ++i)
			static_cast<void>(reserved.CreateEntity());
		reservedCount = scope.Count();
	}
	{
		const AllocationScope scope;
		for (int i = 0; i < kCount; ++i)
			static_cast<void>(control.CreateEntity());
		controlCount = scope.Count();
	}
	EXPECT_LT(reservedCount, controlCount);
	EXPECT_EQ(reserved.EntityCount(), static_cast<std::size_t>(kCount));
}

TEST(World, TrimReturnsChunksAndTheWorldStaysUsable)
{
	constexpr int kCount = 4000; // several chunks
	World         world;
	auto          fill = [&]
	{
		for (int i = 0; i < kCount; ++i)
			world.Add(world.CreateEntity(), Health{ i });
	};
	world.PrepareQuery<Health>();

	fill();
	world.Clear();
	std::size_t warmCount = 0;
	{
		const AllocationScope scope;
		fill(); // chunks are kept by the allocator after Clear
		warmCount = scope.Count();
	}
	EXPECT_EQ(warmCount, 0u);

	world.Clear();
	world.Trim();

	std::size_t coldCount = 0;
	{
		const AllocationScope scope;
		// Assumes all the chunks fit in one block of kChunksPerBlock: Trim then frees it, so it is allocated again.
		fill();
		coldCount = scope.Count();
	}
	EXPECT_GT(coldCount, 0u);
	EXPECT_EQ(world.EntityCount(), static_cast<std::size_t>(kCount));

	int sum = 0;
	world.Query<const Health>().ForEach([&](const Health& health) { sum += health.value; });
	EXPECT_EQ(sum, kCount * (kCount - 1) / 2);
}

TEST(World, CloneIntoANewChunkWhenTheLastChunkIsFull)
{
	const int aliveBefore = Tracked::alive;
	{
		World                 world;
		std::vector<EntityId> entities;
		while (ColdChunkCount(world) < 2)
		{
			const int      value  = static_cast<int>(entities.size());
			const EntityId entity = world.CreateEntity();
			world.Add(entity, Health{ value });
			world.Add(entity, Tracked{}).text = std::to_string(value);
			entities.push_back(entity);
		}
		world.DestroyEntity(entities.back()); // the one that opened the second chunk: the first is now exactly full
		entities.pop_back();
		ASSERT_EQ(ColdChunkCount(world), 1u);

		const EntityId source = entities[5];
		const EntityId clone  = world.CloneEntity(source);
		EXPECT_EQ(ColdChunkCount(world), 2u);
		EXPECT_EQ(world.Get<Health>(clone).value, 5);
		EXPECT_EQ(world.Get<Tracked>(clone).text, "5");

		world.Get<Tracked>(clone).text = "changed";
		EXPECT_EQ(world.Get<Tracked>(source).text, "5");
		EXPECT_EQ(world.Get<Health>(entities.back()).value, static_cast<int>(entities.size()) - 1);
	}
	EXPECT_EQ(Tracked::alive, aliveBefore);
}

TEST(World, ClearThenReuseKeepsArchetypesAndDoesNotAllocate)
{
	constexpr int kCount = 3000;
	World         world;
	auto          fill = [&]
	{
		for (int i = 0; i < kCount; ++i)
		{
			const EntityId entity = world.CreateEntity();
			world.Add(entity, Position{ static_cast<float>(i), 0.f });
			if (i % 2 == 0)
				world.Add(entity, Velocity{ 1.f, 1.f });
		}
	};
	world.PrepareQuery<Position>();
	world.PrepareQuery<Velocity>();

	fill();
	const std::size_t archetypeCount = world.ArchetypeCount();
	world.Clear();
	EXPECT_EQ(world.EntityCount(), 0u);
	EXPECT_EQ(world.Query<Position>().EntityCount(), 0u);

	std::size_t count = 0;
	{
		const AllocationScope scope;
		fill();
		count = scope.Count();
	}
	EXPECT_EQ(count, 0u);
	EXPECT_EQ(world.ArchetypeCount(), archetypeCount);
	EXPECT_EQ(world.Query<Position>().EntityCount(), static_cast<std::size_t>(kCount));
	EXPECT_EQ(world.Query<Velocity>().EntityCount(), static_cast<std::size_t>(kCount / 2));

	float sum = 0.f;
	world.Query<const Position>().ForEach([&](const Position& position) { sum += position.x; });
	EXPECT_EQ(sum, static_cast<float>(kCount) * (kCount - 1) / 2.f);
}

TEST(World, DestroyAllWithOnAMultiChunkColdArchetype)
{
	const int aliveBefore = Tracked::alive;
	{
		World world;
		for (int i = 0; i < 4000; ++i)
		{
			const EntityId entity = world.CreateEntity();
			world.Add(entity, Health{ i });
			world.Add(entity, Tracked{});
		}
		ASSERT_GE(ColdChunkCount(world), 2u);

		std::vector<EntityId> survivors;
		for (int i = 0; i < 10; ++i)
		{
			survivors.push_back(world.CreateEntity());
			world.Add(survivors.back(), Health{ 1000 + i });
		}

		world.DestroyAllWith<Tracked>();
		EXPECT_EQ(Tracked::alive, aliveBefore);
		EXPECT_EQ(world.EntityCount(), survivors.size());
		for (std::size_t i = 0; i < survivors.size(); ++i)
		{
			ASSERT_TRUE(world.IsAlive(survivors[i]));
			EXPECT_EQ(world.Get<Health>(survivors[i]).value, 1000 + static_cast<int>(i));
		}
	}
	EXPECT_EQ(Tracked::alive, aliveBefore);
}

TEST(World, StructuralChangesInTheMiddleOfAChunkFixTheMovedEntityRecord)
{
	World                 world;
	std::vector<EntityId> entities;
	while (world.Query<Health>().ChunkCount() < 2 || entities.size() < 50)
	{
		entities.push_back(world.CreateEntity());
		world.Add(entities.back(), Health{ static_cast<int>(entities.size()) - 1 });
	}
	entities.push_back(world.CreateEntity()); // one more row in the last chunk
	world.Add(entities.back(), Health{ static_cast<int>(entities.size()) - 1 });

	auto verify = [&](EntityId except)
	{
		for (std::size_t i = 0; i < entities.size(); ++i)
			if (entities[i] != except)
				EXPECT_EQ(world.Get<Health>(entities[i]).value, static_cast<int>(i)) << "entity " << i;
	};

	const EntityId victim = entities[2]; // first chunk, not its last row: the hole is filled from the last chunk
	world.Add<Enemy>(victim);
	EXPECT_TRUE(world.Has<Enemy>(victim));
	EXPECT_EQ(world.Get<Health>(victim).value, 2);
	verify(victim);

	world.Add(victim, Velocity{ 5.f, 6.f });
	EXPECT_EQ(world.Get<Velocity>(victim).dy, 6.f);
	verify(victim);

	world.Remove<Velocity>(victim);
	EXPECT_EQ(world.Get<Health>(victim).value, 2);
	verify(victim);

	world.Remove<Enemy>(victim);
	EXPECT_EQ(world.Get<Health>(victim).value, 2);
	verify(EntityId{});
}
