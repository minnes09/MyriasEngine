// The engine runs one World today but must support N at once (multiplayer: server/client
// worlds, instances). These tests keep that property from regressing.

#include "ECS/World.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

namespace
{
struct Position
{
	float x;
	float y;
};
} // namespace

TEST(MultiWorld, WorldsHaveDistinctIdsAndIndependentData)
{
	World first;
	World second;
	EXPECT_NE(first.GetWorldId(), second.GetWorldId());

	const EntityId a = first.CreateEntity();
	const EntityId b = second.CreateEntity();
	first.Add(a, Position{ 1.f, 0.f });
	second.Add(b, Position{ 2.f, 0.f });

	EXPECT_EQ(a.Index(), b.Index()); // same slot in each World...
	EXPECT_NE(a, b);                 // ...but different ids
	EXPECT_EQ(first.Get<Position>(a).x, 1.f);
	EXPECT_EQ(second.Get<Position>(b).x, 2.f);
	EXPECT_EQ(first.EntityCount(), 1u);
	EXPECT_EQ(second.EntityCount(), 1u);
}

TEST(MultiWorld, ForeignIdIsNotAlive)
{
	World          first;
	World          second;
	const EntityId entity = first.CreateEntity();
	static_cast<void>(second.CreateEntity()); // same index alive in the second World
	EXPECT_FALSE(second.IsAlive(entity));
}

TEST(MultiWorldDeathTest, UsingAForeignIdAsserts)
{
	World          first;
	World          second;
	const EntityId entity = first.CreateEntity();
	EXPECT_DEBUG_DEATH(second.DestroyEntity(entity), "another World");
}

TEST(MultiWorld, DestroyingAWorldLeavesOthersIntact)
{
	auto           doomed = std::make_unique<World>();
	World          survivor;
	const EntityId kept = survivor.CreateEntity();
	survivor.Add(kept, Position{ 5.f, 6.f });
	doomed->Add(doomed->CreateEntity(), Position{});

	doomed.reset();
	ASSERT_TRUE(survivor.IsAlive(kept));
	EXPECT_EQ(survivor.Get<Position>(kept).y, 6.f);
}

TEST(MultiWorld, ReleasedIdsCanBeReused)
{
	std::vector<std::unique_ptr<World>> worlds;
	for (int i = 0; i < 255; ++i)
		worlds.push_back(std::make_unique<World>());

	worlds.pop_back();                            // frees one id...
	worlds.push_back(std::make_unique<World>());  // ...so the 255th World fits again
	EXPECT_EQ(worlds.size(), 255u);
}

TEST(MultiWorldDeathTest, MoreThan255WorldsAbort)
{
	EXPECT_DEATH(
	    {
		    std::vector<std::unique_ptr<World>> worlds;
		    for (int i = 0; i < 256; ++i)
			    worlds.push_back(std::make_unique<World>());
	    },
	    "too many Worlds");
}

TEST(MultiWorld, ComponentIdsAreSharedAcrossWorlds)
{
	World          first;
	World          second;
	const EntityId a = first.CreateEntity();
	const EntityId b = second.CreateEntity();
	first.Add(a, Position{});
	second.Add(b, Position{});
	// Same type, same id: signatures are comparable between Worlds.
	EXPECT_TRUE(first.Has<Position>(a));
	EXPECT_TRUE(second.Has<Position>(b));
	EXPECT_EQ(GetComponentId<Position>(), GetComponentId<const Position>());
}
