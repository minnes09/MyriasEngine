#include "ECS/EntityId.h"

#include <gtest/gtest.h>

#include <functional>
#include <limits>

static_assert(sizeof(EntityId) == 8);
static_assert(EntityId{}.IsNull());
static_assert(EntityId::Create(5, 1, 1).Index() == 5);

TEST(EntityId, DefaultIsNullAndCreatedIsNot)
{
	EXPECT_TRUE(EntityId{}.IsNull());
	EXPECT_FALSE(EntityId::Create(0, 1, 1).IsNull());
}

TEST(EntityId, PackUnpackAtLimits)
{
	constexpr auto kMaxIndex      = std::numeric_limits<std::uint32_t>::max();
	constexpr auto kMaxGeneration = std::numeric_limits<std::uint16_t>::max();

	const EntityId id = EntityId::Create(kMaxIndex, kMaxGeneration, 255, 0xAB);
	EXPECT_EQ(id.Index(), kMaxIndex);
	EXPECT_EQ(id.Generation(), kMaxGeneration);
	EXPECT_EQ(id.GetWorldId(), 255);
	EXPECT_EQ(id.Flags(), 0xAB);
}

TEST(EntityId, FieldsDoNotOverlap)
{
	const EntityId id = EntityId::Create(1, 2, 3, 4);
	EXPECT_EQ(id.Index(), 1u);
	EXPECT_EQ(id.Generation(), 2u);
	EXPECT_EQ(id.GetWorldId(), 3);
	EXPECT_EQ(id.Flags(), 4);
}

TEST(EntityId, EqualityUsesIndexGenerationAndWorld)
{
	const EntityId id = EntityId::Create(10, 1, 1);
	EXPECT_EQ(id, EntityId::Create(10, 1, 1));
	EXPECT_NE(id, EntityId::Create(11, 1, 1)); // other slot
	EXPECT_NE(id, EntityId::Create(10, 2, 1)); // stale: slot reused
	EXPECT_NE(id, EntityId::Create(10, 1, 2)); // same slot in another World
}

TEST(EntityId, FlagsAreIgnoredByEqualityAndHash)
{
	const EntityId plain  = EntityId::Create(10, 1, 1, 0);
	const EntityId tagged = EntityId::Create(10, 1, 1, 0x80);
	EXPECT_EQ(plain, tagged);
	EXPECT_EQ(std::hash<EntityId>{}(plain), std::hash<EntityId>{}(tagged));
}

TEST(EntityId, FlagsOnlyIdIsNullAndEqualsDefault)
{
	// Create rejects generation 0 and world 0 when asserts are on, so the id is only buildable without them.
	if constexpr (MYRIAS_ASSERTS_ENABLED)
		GTEST_SKIP() << "an id with only flags cannot be built while asserts are enabled";
	else
	{
		const EntityId flagsOnly = EntityId::Create(0, 0, 0, 0x80);
		EXPECT_EQ(flagsOnly.Flags(), 0x80);
		EXPECT_TRUE(flagsOnly.IsNull());
		EXPECT_EQ(flagsOnly, EntityId{});
	}
}

TEST(EntityIdDeathTest, ZeroGenerationAsserts)
{
	EXPECT_DEBUG_DEATH(static_cast<void>(EntityId::Create(1, 0, 1)), "generation 0 is reserved");
}
