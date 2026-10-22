#include "ECS/EntityIndex.h"

#include "Support/AllocationCounter.h"

#include <gtest/gtest.h>

#include <limits>
#include <set>
#include <vector>

TEST(EntityIndex, CreatedIdsAreDistinctAndCarryTheWorld)
{
	EntityIndex index(3);
	std::set<std::uint32_t> slots;
	for (int i = 0; i < 10; ++i)
	{
		const EntityId entity = index.Create();
		EXPECT_EQ(entity.GetWorldId(), 3);
		EXPECT_TRUE(index.IsAlive(entity));
		slots.insert(entity.Index());
	}
	EXPECT_EQ(slots.size(), 10u);
	EXPECT_EQ(index.AliveCount(), 10u);
}

TEST(EntityIndex, DestroyedIdIsNotAlive)
{
	EntityIndex    index(1);
	const EntityId entity = index.Create();
	index.Destroy(entity);
	EXPECT_FALSE(index.IsAlive(entity));
	EXPECT_EQ(index.Find(entity), nullptr);
	EXPECT_EQ(index.AliveCount(), 0u);
}

TEST(EntityIndex, RecycledSlotGetsNewGeneration)
{
	EntityIndex    index(1);
	const EntityId first = index.Create();
	index.Destroy(first);

	const EntityId second = index.Create();
	EXPECT_EQ(second.Index(), first.Index());
	EXPECT_EQ(second.Generation(), first.Generation() + 1);
	EXPECT_FALSE(index.IsAlive(first)); // stale id
	EXPECT_TRUE(index.IsAlive(second));
}

TEST(EntityIndex, DestroyInTheMiddleKeepsOthersAlive)
{
	EntityIndex    index(1);
	const EntityId a = index.Create();
	const EntityId b = index.Create();
	const EntityId c = index.Create();

	index.Destroy(b);
	EXPECT_TRUE(index.IsAlive(a));
	EXPECT_FALSE(index.IsAlive(b));
	EXPECT_TRUE(index.IsAlive(c));
}

TEST(EntityIndex, SlotIsRetiredWhenGenerationSaturates)
{
	EntityIndex index(1);
	EntityId    entity = index.Create();
	const auto  slot   = entity.Index();
	while (entity.Generation() < std::numeric_limits<std::uint16_t>::max())
	{
		index.Destroy(entity);
		entity = index.Create();
		ASSERT_EQ(entity.Index(), slot);
	}
	index.Destroy(entity); // last generation: the slot is retired

	const EntityId next = index.Create();
	EXPECT_NE(next.Index(), slot);
	EXPECT_FALSE(index.IsAlive(entity));
}

TEST(EntityIndex, WorksAcrossPages)
{
	EntityIndex           index(1);
	std::vector<EntityId> entities;
	for (int i = 0; i < 5000; ++i)
		entities.push_back(index.Create());

	for (const EntityId entity : entities)
		ASSERT_NE(index.Find(entity), nullptr);
	EXPECT_EQ(entities.back().Index(), 4999u);
}

TEST(EntityIndex, IdFromAnotherWorldIsNotAlive)
{
	EntityIndex    index(1);
	const EntityId entity = index.Create();
	const EntityId foreign = EntityId::Create(entity.Index(), entity.Generation(), 2);
	EXPECT_FALSE(index.IsAlive(foreign));
	EXPECT_EQ(index.Find(foreign), nullptr);
}

TEST(EntityIndex, RecordIsStoredAndResetOnReuse)
{
	EntityIndex    index(1);
	const EntityId entity   = index.Create();
	EXPECT_EQ(index.Get(entity).archetype, nullptr);

	index.Get(entity).row = { .chunkIndex = 2, .row = 7 };
	EXPECT_EQ(index.Find(entity)->row, (ArchetypeRow{ .chunkIndex = 2, .row = 7 }));

	index.Destroy(entity);
	const EntityId reused = index.Create();
	EXPECT_EQ(index.Get(reused).row, ArchetypeRow{});
}

TEST(EntityIndex, ClearKillsEverythingAndReusesIndices)
{
	EntityIndex           index(1);
	std::vector<EntityId> entities;
	for (int i = 0; i < 20; ++i)
		entities.push_back(index.Create());

	index.Clear();
	EXPECT_EQ(index.AliveCount(), 0u);
	for (const EntityId entity : entities)
		EXPECT_FALSE(index.IsAlive(entity));
	EXPECT_LT(index.Create().Index(), 20u);
}

TEST(EntityIndex, SteadyStateDoesNotAllocate)
{
	EntityIndex index(1);
	index.Reserve(1000);
	std::vector<EntityId> entities;
	entities.reserve(1000);

	auto cycle = [&]
	{
		for (int i = 0; i < 1000; ++i)
			entities.push_back(index.Create());
		for (const EntityId entity : entities)
			index.Destroy(entity);
		entities.clear();
	};
	cycle(); // warm-up: pages

	std::size_t count = 0;
	{
		const AllocationScope scope;
		cycle();
		count = scope.Count();
	}
	EXPECT_EQ(count, 0u);
}

TEST(EntityIndex, ReserveMakesTheFirstCreatesAllocationFree)
{
	constexpr std::size_t kCount = 10000; // several pages
	EntityIndex           index(1);
	index.Reserve(kCount);

	std::size_t count = 0;
	{
		const AllocationScope scope;
		for (std::size_t i = 0; i < kCount; ++i)
			static_cast<void>(index.Create());
		count = scope.Count();
	}
	EXPECT_EQ(count, 0u);
	EXPECT_EQ(index.AliveCount(), kCount);
}

TEST(EntityIndex, RetiringASlotWhileOtherIdsAreFreeKeepsTheIndexConsistent)
{
	EntityIndex    index(1);
	EntityId       a = index.Create();
	const EntityId b = index.Create();
	const EntityId c = index.Create();
	const EntityId d = index.Create();
	const auto     retiredSlot = a.Index();

	while (a.Generation() < std::numeric_limits<std::uint16_t>::max())
	{
		index.Destroy(a);
		a = index.Create();
		ASSERT_EQ(a.Index(), retiredSlot);
	}

	// The free part now holds b and c, so retiring a happens with lastAlive != dense.size() - 1.
	index.Destroy(b);
	index.Destroy(c);
	index.Destroy(a);
	EXPECT_EQ(index.AliveCount(), 1u);
	EXPECT_TRUE(index.IsAlive(d));
	EXPECT_FALSE(index.IsAlive(a));

	// The other free ids are reused with their next generation; the retired slot never comes back.
	const EntityId first  = index.Create();
	const EntityId second = index.Create();
	EXPECT_EQ(std::set<std::uint32_t>({ first.Index(), second.Index() }),
	          std::set<std::uint32_t>({ b.Index(), c.Index() }));
	EXPECT_EQ(first.Generation(), 2);
	EXPECT_EQ(second.Generation(), 2);
	EXPECT_FALSE(index.IsAlive(b));
	EXPECT_FALSE(index.IsAlive(c));

	const EntityId third = index.Create();
	EXPECT_NE(third.Index(), retiredSlot);
	for (const EntityId alive : { d, first, second, third })
		EXPECT_TRUE(index.IsAlive(alive));
	EXPECT_EQ(index.AliveCount(), 4u);

	// Records stay reachable through the swapped dense entries.
	index.Destroy(d);
	for (const EntityId alive : { first, second, third })
		EXPECT_TRUE(index.IsAlive(alive));
	EXPECT_FALSE(index.IsAlive(d));
}
