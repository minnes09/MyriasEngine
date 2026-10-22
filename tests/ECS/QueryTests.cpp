#include "ECS/World.h"

#include "Support/AllocationCounter.h"

#include <gtest/gtest.h>

#include <atomic>
#include <latch>
#include <cstddef>
#include <thread>
#include <vector>

namespace
{
struct Position
{
	float x = 0.f;
	float y = 0.f;
};

struct Velocity
{
	float dx = 0.f;
	float dy = 0.f;
};

struct Health
{
	int value = 0;
};

struct Enemy
{
};

struct Dead
{
};

// Entity with Position (value i) and optionally the others.
EntityId Spawn(World& world, float x)
{
	const EntityId entity = world.CreateEntity();
	world.Add(entity, Position{ x, 0.f });
	return entity;
}
} // namespace

TEST(Query, JoinsTwoTermsWithValues)
{
	World          world;
	const EntityId a = Spawn(world, 1.f);
	world.Add(a, Velocity{ 10.f, 0.f });
	const EntityId b = Spawn(world, 2.f);
	world.Add(b, Velocity{ 20.f, 0.f });
	Spawn(world, 3.f); // no Velocity: not matched

	int visited = 0;
	world.Query<Position, const Velocity>().ForEach(
	    [&](Position& p, const Velocity& v)
	    {
		    p.x += v.dx;
		    ++visited;
	    });

	EXPECT_EQ(visited, 2);
	EXPECT_EQ(world.Get<Position>(a).x, 11.f);
	EXPECT_EQ(world.Get<Position>(b).x, 22.f);
}

TEST(Query, JoinsThreeTerms)
{
	World          world;
	const EntityId a = Spawn(world, 1.f);
	world.Add(a, Velocity{ 2.f, 3.f });
	world.Add(a, Health{ 4 });
	const EntityId b = Spawn(world, 5.f);
	world.Add(b, Velocity{ 6.f, 7.f }); // no Health

	float sum = 0.f;
	world.Query<const Position, const Velocity, const Health>().ForEach(
	    [&](const Position& p, const Velocity& v, const Health& h)
	    { sum += p.x + v.dx + v.dy + static_cast<float>(h.value); });
	EXPECT_EQ(sum, 10.f);
}

TEST(Query, OptionalIsNullWhenAbsent)
{
	World          world;
	const EntityId with = Spawn(world, 1.f);
	world.Add(with, Health{ 9 });
	Spawn(world, 2.f);

	int present = 0;
	int absent  = 0;
	world.Query<const Position, Optional<Health>>().ForEach(
	    [&](const Position&, Health* health)
	    {
		    if (health)
		    {
			    EXPECT_EQ(health->value, 9);
			    ++present;
		    }
		    else
			    ++absent;
	    });
	EXPECT_EQ(present, 1);
	EXPECT_EQ(absent, 1);
}

TEST(Query, WithAndWithoutFilter)
{
	World          world;
	const EntityId plain = Spawn(world, 1.f);
	const EntityId enemy = Spawn(world, 2.f);
	world.Add(enemy, Enemy{});
	const EntityId deadEnemy = Spawn(world, 3.f);
	world.Add(deadEnemy, Enemy{});
	world.Add(deadEnemy, Dead{});

	std::vector<float> seen;
	world.Query<const Position, With<Enemy>, Without<Dead>>().ForEach([&](const Position& p) { seen.push_back(p.x); });
	ASSERT_EQ(seen.size(), 1u);
	EXPECT_EQ(seen[0], 2.f);

	seen.clear();
	world.Query<const Position, Without<Enemy>>().ForEach([&](const Position& p) { seen.push_back(p.x); });
	ASSERT_EQ(seen.size(), 1u);
	EXPECT_EQ(seen[0], 1.f);
	static_cast<void>(plain);
}

TEST(Query, CallbackWithEntityId)
{
	World          world;
	const EntityId a = Spawn(world, 1.f);
	const EntityId b = Spawn(world, 2.f);

	std::vector<EntityId> ids;
	world.Query<Position>().ForEach([&](EntityId entity, Position&) { ids.push_back(entity); });
	ASSERT_EQ(ids.size(), 2u);
	EXPECT_TRUE((ids[0] == a && ids[1] == b) || (ids[0] == b && ids[1] == a));

	ids.clear();
	world.Query<Position, Optional<Health>, With<Enemy>>().ForEach(
	    [&](EntityId entity, Position&, Health*) { ids.push_back(entity); });
	EXPECT_TRUE(ids.empty());
}

TEST(Query, ArchetypeCreatedAfterTheFirstCallIsIncluded)
{
	World          world;
	const EntityId a = Spawn(world, 1.f);

	int count = 0;
	world.Query<Position>().ForEach([&](Position&) { ++count; });
	EXPECT_EQ(count, 1);

	world.Add(a, Velocity{}); // new archetype {Position, Velocity}
	Spawn(world, 2.f);
	count = 0;
	world.Query<Position>().ForEach([&](Position&) { ++count; });
	EXPECT_EQ(count, 2);
}

TEST(Query, EmptyArchetypesAreSkipped)
{
	World          world;
	const EntityId a = Spawn(world, 1.f);
	world.Add(a, Velocity{});
	world.DestroyEntity(a); // archetypes {Position} and {Position, Velocity} exist but are empty

	int calls = 0;
	world.Query<Position>().ForEach([&](Position&) { ++calls; });
	world.Query<Position>().ForEachChunk([&](const ChunkView<Position>&) { ++calls; });
	EXPECT_EQ(calls, 0);
	EXPECT_EQ(world.Query<Position>().EntityCount(), 0u);
	EXPECT_EQ(world.Query<Position>().ChunkCount(), 0u);
}

TEST(Query, ForEachAndForEachChunkAgree)
{
	World world;
	for (int i = 0; i < 5000; ++i) // several chunks, two archetypes
	{
		const EntityId entity = Spawn(world, static_cast<float>(i));
		if (i % 3 == 0)
			world.Add(entity, Velocity{ 1.f, 1.f });
	}

	double rowSum = 0.0;
	world.Query<const Position>().ForEach([&](const Position& p) { rowSum += p.x; });

	double chunkSum = 0.0;
	world.Query<const Position>().ForEachChunk(
	    [&](const ChunkView<const Position>& chunk)
	    {
		    const std::span<const Position> positions = chunk.Column<const Position>();
		    EXPECT_EQ(positions.size(), chunk.Count());
		    EXPECT_EQ(chunk.Entities().size(), chunk.Count());
		    for (std::size_t i = 0; i < chunk.Count(); ++i)
			    chunkSum += positions[i].x;
	    });

	EXPECT_EQ(rowSum, chunkSum);
	EXPECT_EQ(rowSum, 5000.0 * 4999.0 / 2.0);
}

TEST(Query, ChunkViewOptionalColumnIsEmptyWhenAbsent)
{
	World          world;
	const EntityId a = Spawn(world, 1.f);
	world.Add(a, Health{ 3 });
	Spawn(world, 2.f);

	std::size_t withColumn    = 0;
	std::size_t withoutColumn = 0;
	world.Query<Position, Optional<Health>>().ForEachChunk(
	    [&](const ChunkView<Position, Optional<Health>>& chunk)
	    {
		    if (chunk.Column<Health>().empty())
			    ++withoutColumn;
		    else
		    {
			    ++withColumn;
			    EXPECT_EQ(chunk.Column<Health>()[0].value, 3);
		    }
	    });
	EXPECT_EQ(withColumn, 1u);
	EXPECT_EQ(withoutColumn, 1u);
}

TEST(Query, WorksOnAConstWorld)
{
	World world;
	Spawn(world, 4.f);
	const World& readOnly = world;

	float sum = 0.f;
	readOnly.Query<const Position>().ForEach([&](const Position& p) { sum += p.x; });
	EXPECT_EQ(sum, 4.f);
	EXPECT_EQ(readOnly.Query<const Position>().EntityCount(), 1u);
}

TEST(Query, SameQueryOnTwoWorldsIsIndependent)
{
	World first;
	World second;
	Spawn(first, 1.f);
	Spawn(first, 2.f);
	Spawn(second, 10.f);

	float firstSum  = 0.f;
	float secondSum = 0.f;
	first.Query<const Position>().ForEach([&](const Position& p) { firstSum += p.x; });
	second.Query<const Position>().ForEach([&](const Position& p) { secondSum += p.x; });
	EXPECT_EQ(firstSum, 3.f);
	EXPECT_EQ(secondSum, 10.f);
	EXPECT_EQ(first.Query<const Position>().EntityCount(), 2u);
	EXPECT_EQ(second.Query<const Position>().EntityCount(), 1u);
}

TEST(Query, TwoLiveQueriesStayValid)
{
	World world;
	Spawn(world, 1.f);
	const auto positions  = world.Query<const Position>();
	const auto velocities = world.Query<const Velocity>(); // creates another cache: must not move the first
	EXPECT_EQ(positions.EntityCount(), 1u);
	EXPECT_EQ(velocities.EntityCount(), 0u);
}

TEST(Query, CountsEntitiesAndChunks)
{
	World world;
	const auto chunkCountWithOneEntity = [&]
	{
		const EntityId    probe = Spawn(world, 0.f);
		const std::size_t value = world.Query<Position>().ChunkCount();
		world.DestroyEntity(probe);
		return value;
	};
	EXPECT_EQ(chunkCountWithOneEntity(), 1u);

	for (int i = 0; i < 3000; ++i)
		Spawn(world, 1.f);
	const auto query = world.Query<Position>();
	EXPECT_EQ(query.EntityCount(), 3000u);
	EXPECT_GE(query.ChunkCount(), 2u); // 8 bytes + 8-byte id per row: about 1000 rows per chunk
	EXPECT_LE(query.ChunkCount(), 4u);
}

TEST(Query, StructuralChangeWhileIteratingAsserts)
{
	World world;
	Spawn(world, 1.f);
	EXPECT_DEBUG_DEATH(world.Query<Position>().ForEach([&](Position&) { static_cast<void>(world.CreateEntity()); }),
	                   "while iterating");
}

TEST(Query, IterationDoesNotAllocateInSteadyState)
{
	World world;
	for (int i = 0; i < 3000; ++i)
	{
		const EntityId entity = Spawn(world, 1.f);
		if (i % 2 == 0)
			world.Add(entity, Velocity{ 1.f, 1.f });
	}
	float sum = 0.f;
	auto  run = [&]
	{
		world.Query<const Position, Optional<const Velocity>>().ForEach(
		    [&](const Position& p, const Velocity* v) { sum += p.x + (v ? v->dx : 0.f); });
		world.Query<const Position, Optional<const Velocity>>().ForEachChunk(
		    [&](const ChunkView<const Position, Optional<const Velocity>>& chunk)
		    {
			    for (const Position& p : chunk.Column<const Position>())
				    sum += p.x;
		    });
	};
	run(); // warms the cache

	std::size_t count = 0;
	{
		const AllocationScope scope;
		run();
		count = scope.Count();
	}
	EXPECT_EQ(count, 0u);
	EXPECT_GT(sum, 0.f);
}

TEST(Query, ConstQueriesAreSafeFromManyThreads)
{
	World world;
	for (int i = 0; i < 2000; ++i)
	{
		const EntityId entity = Spawn(world, 1.f);
		if (i % 2 == 0)
			world.Add(entity, Velocity{ 2.f, 0.f });
		if (i % 4 == 0)
			world.Add(entity, Enemy{});
	}
	const World& readOnly = world; // none of these query types has been used yet: the caches are created concurrently

	constexpr int                threadCount = 8;
	std::vector<std::thread>     threads;
	std::vector<double>          sums(threadCount, 0.0);
	std::vector<std::size_t>     counts(threadCount, 0);
	for (int t = 0; t < threadCount; ++t)
	{
		threads.emplace_back(
		    [&, t]
		    {
			    for (int round = 0; round < 50; ++round)
			    {
				    double sum = 0.0;
				    if (t % 2 == 0)
					    readOnly.Query<const Position, const Velocity>().ForEach(
					        [&](const Position& p, const Velocity& v) { sum += p.x + v.dx; });
				    else
					    readOnly.Query<const Position, With<Enemy>>().ForEachChunk(
					        [&](const ChunkView<const Position, With<Enemy>>& chunk)
					        {
						        for (const Position& p : chunk.Column<const Position>())
							        sum += p.x;
					        });
				    sums[t] = sum;
				    counts[t] = readOnly.Query<const Position, const Velocity>().EntityCount();
			    }
		    });
	}
	for (std::thread& thread : threads)
		thread.join();

	for (int t = 0; t < threadCount; ++t)
	{
		EXPECT_EQ(sums[t], t % 2 == 0 ? 1000.0 * 3.0 : 500.0);
		EXPECT_EQ(counts[t], 1000u);
	}
}

TEST(Query, AnExistingQuerySeesArchetypesCreatedLater)
{
	World          world;
	const EntityId a = Spawn(world, 1.f);
	const auto     query = world.Query<const Position>();
	EXPECT_EQ(query.EntityCount(), 1u);

	world.Add(a, Velocity{});    // new archetype
	world.Add(a, Health{ 1 });   // another one
	Spawn(world, 2.f);

	EXPECT_EQ(query.EntityCount(), 2u);
	float sum = 0.f;
	query.ForEach([&](const Position& p) { sum += p.x; });
	EXPECT_EQ(sum, 3.f);
}

TEST(Query, PrepareQueryBuildsTheCacheAhead)
{
	World world;
	for (int i = 0; i < 10; ++i)
		world.Add(world.CreateEntity(), Position{});
	world.PrepareQuery<Position, Optional<const Health>, Without<Dead>>();

	std::size_t count = 0;
	{
		const AllocationScope scope;
		const auto            query = world.Query<Position, Optional<const Health>, Without<Dead>>();
		count                       = scope.Count();
		static_cast<void>(query);
	}
	EXPECT_EQ(count, 0u);
	EXPECT_EQ((world.Query<Position, Optional<const Health>, Without<Dead>>().EntityCount()), 10u);
}

TEST(Query, NestedIterationsReleaseTheGuardOnlyAtTheEnd)
{
	World          world;
	const EntityId a = Spawn(world, 1.f);
	world.Add(a, Velocity{});
	Spawn(world, 2.f);

	int pairs = 0;
	world.Query<const Position>().ForEach(
	    [&](const Position&)
	    {
		    world.Query<const Position>().ForEach([&](const Position&) { ++pairs; }); // same query
		    world.Query<const Velocity>().ForEach([&](const Velocity&) { ++pairs; }); // another one
	    });
	EXPECT_EQ(pairs, 2 * 2 + 2 * 1);
	static_cast<void>(world.CreateEntity()); // both guards released
	EXPECT_EQ(world.EntityCount(), 3u);
}

TEST(Query, StructuralChangeInsideForEachChunkAsserts)
{
	World world;
	Spawn(world, 1.f);
	EXPECT_DEBUG_DEATH(world.Query<Position>().ForEachChunk(
	                       [&](const ChunkView<Position>&) { static_cast<void>(world.CreateEntity()); }),
	                   "while iterating");
}

TEST(Query, TheGuardOfOneWorldDoesNotBlockAnother)
{
	World first;
	World second;
	Spawn(first, 1.f);

	first.Query<Position>().ForEach([&](Position&) { Spawn(second, 5.f); }); // structural change on another World
	EXPECT_EQ(second.EntityCount(), 1u);
}

TEST(Query, FilterOnlyQueries)
{
	World          world;
	const EntityId a = Spawn(world, 1.f);
	world.Add(a, Health{ 1 });
	Spawn(world, 2.f);

	std::size_t visited = 0;
	world.Query<With<Health>>().ForEach([&](EntityId entity) { visited += world.IsAlive(entity) ? 1 : 0; });
	EXPECT_EQ(visited, 1u);

	visited = 0;
	world.Query<With<Position>, Without<Health>>().ForEach([&] { ++visited; });
	EXPECT_EQ(visited, 1u);

	world.Query<With<Position>>().ForEachChunk([&](const ChunkView<With<Position>>& chunk) { visited += chunk.Count(); });
	EXPECT_EQ(visited, 3u);
}

TEST(Query, ConcurrentFirstUseSmokeTest) // smoke test: not a proof of the absence of races (no TSan on Windows)
{
	World world;
	for (int i = 0; i < 1000; ++i)
	{
		const EntityId entity = Spawn(world, 1.f);
		if (i % 2 == 0)
			world.Add(entity, Health{ 1 });
		if (i % 5 == 0)
			world.Add(entity, Dead{});
	}
	const World& readOnly = world;

	constexpr int            threadCount = 8;
	std::latch               start(threadCount);
	std::vector<std::thread> threads;
	std::vector<std::size_t> healthy(threadCount, 0);
	std::vector<std::size_t> alive(threadCount, 0);
	std::vector<std::size_t> optional(threadCount, 0);
	std::vector<std::size_t> rowsSeen(threadCount, 0);
	for (int t = 0; t < threadCount; ++t)
	{
		threads.emplace_back(
		    [&, t]
		    {
			    start.arrive_and_wait(); // everyone hits the first Query() of each type together
			    healthy[t]  = readOnly.Query<const Position, const Health>().EntityCount();
			    alive[t]    = readOnly.Query<const Position, Without<Dead>>().EntityCount();
			    optional[t] = readOnly.Query<const Health, Optional<const Position>, Without<Dead>>().EntityCount();
			    readOnly.Query<const Position, Without<Dead>>().ForEach([&](const Position&) { ++rowsSeen[t]; });
		    });
	}
	for (std::thread& thread : threads)
		thread.join();

	for (int t = 0; t < threadCount; ++t)
	{
		EXPECT_EQ(healthy[t], 500u);
		EXPECT_EQ(alive[t], 800u);
		EXPECT_EQ(optional[t], 400u); // Health and not Dead: 500 - 100
		EXPECT_EQ(rowsSeen[t], 800u);
	}
}
