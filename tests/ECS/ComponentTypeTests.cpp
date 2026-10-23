#include "ECS/ComponentType.h"
#include "ECS/ComponentTypeRegistry.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <latch>
#include <set>
#include <string>
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
	float dx;
	float dy;
};

struct WithDefaults // trivially copyable, but its default constructor is not trivial
{
	int characterSize = 16;
};

struct Tag
{
};

// Cold component: owns heap data, counted to detect leaks.
struct Tracked
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

struct NotMarkedCold
{
	std::string text;
};

template <int N>
struct ThreadComponent
{
	int value;
};
} // namespace

template <>
inline constexpr bool IsColdComponent<Tracked> = true; // test type: exercises the non-trivial paths

static_assert(IsComponent<Position>);
static_assert(IsComponent<Tag>);
static_assert(IsComponent<Tracked>);
static_assert(!IsComponent<NotMarkedCold>, "non-trivial types need the IsColdComponent opt-in");
static_assert(!IsComponent<const Position>);
static_assert(!IsComponent<Position&>);

TEST(ComponentType, IdsAreDistinctAndStable)
{
	const ComponentId position = GetComponentId<Position>();
	const ComponentId velocity = GetComponentId<Velocity>();
	EXPECT_NE(position, velocity);
	EXPECT_EQ(position, GetComponentId<Position>());
	EXPECT_LT(position, ComponentTypeRegistry::Count());
}

TEST(ComponentType, ConstSharesTheId)
{
	EXPECT_EQ(GetComponentId<const Position>(), GetComponentId<Position>());
}

TEST(ComponentType, MaskHasExactlyTheRequestedBits)
{
	const ComponentMask mask = MakeComponentMask<Position, Velocity>();
	EXPECT_EQ(mask.count(), 2u);
	EXPECT_TRUE(mask.test(GetComponentId<Position>()));
	EXPECT_TRUE(mask.test(GetComponentId<Velocity>()));
	EXPECT_FALSE(mask.test(GetComponentId<Tag>()));
}

TEST(ComponentType, TrivialMetaUsesNoFunctions)
{
	const ComponentMeta& meta = GetComponentMeta(GetComponentId<Velocity>());
	EXPECT_EQ(meta.size, sizeof(Velocity));
	EXPECT_EQ(meta.alignment, alignof(Velocity));
	EXPECT_TRUE(meta.bTriviallyCopyable);
	EXPECT_FALSE(meta.bTag);
	EXPECT_EQ(meta.defaultConstruct, nullptr);
	EXPECT_EQ(meta.moveConstruct, nullptr);
	EXPECT_EQ(meta.copyConstruct, nullptr);
	EXPECT_EQ(meta.destroy, nullptr);
}

TEST(ComponentType, DefaultInitializersUseDefaultConstruct)
{
	const ComponentMeta& meta = GetComponentMeta(GetComponentId<WithDefaults>());
	EXPECT_TRUE(meta.bTriviallyCopyable);
	ASSERT_NE(meta.defaultConstruct, nullptr);

	alignas(WithDefaults) std::byte storage[sizeof(WithDefaults) * 2]{};
	meta.defaultConstruct(storage, 2);
	const auto* values = reinterpret_cast<const WithDefaults*>(storage);
	EXPECT_EQ(values[0].characterSize, 16);
	EXPECT_EQ(values[1].characterSize, 16);
}

TEST(ComponentType, EmptyTypeIsTag)
{
	EXPECT_TRUE(GetComponentMeta(GetComponentId<Tag>()).bTag);
}

TEST(ComponentType, ColdMetaManagesLifetimeWithoutLeaks)
{
	const ComponentMeta& meta = GetComponentMeta(GetComponentId<Tracked>());
	ASSERT_FALSE(meta.bTriviallyCopyable);
	ASSERT_NE(meta.defaultConstruct, nullptr);
	ASSERT_NE(meta.moveConstruct, nullptr);
	ASSERT_NE(meta.copyConstruct, nullptr);
	ASSERT_NE(meta.destroy, nullptr);

	const int aliveBefore = Tracked::alive;
	{
		alignas(Tracked) std::byte a[sizeof(Tracked) * 2];
		alignas(Tracked) std::byte b[sizeof(Tracked) * 2];
		alignas(Tracked) std::byte c[sizeof(Tracked) * 2];

		meta.defaultConstruct(a, 2);
		meta.copyConstruct(b, a, 2);
		meta.moveConstruct(c, a, 2);
		EXPECT_EQ(reinterpret_cast<const Tracked*>(b)[1].text, "tracked");
		EXPECT_EQ(reinterpret_cast<const Tracked*>(c)[1].text, "tracked");
		EXPECT_EQ(Tracked::alive, aliveBefore + 6);

		meta.destroy(a, 2);
		meta.destroy(b, 2);
		meta.destroy(c, 2);
	}
	EXPECT_EQ(Tracked::alive, aliveBefore);
}

TEST(ComponentType, NameComesFromTheType)
{
	const ComponentMeta& meta = GetComponentMeta(GetComponentId<Position>());
	EXPECT_FALSE(meta.name.IsNull());
	if constexpr (MYRIAS_ASSERTS_ENABLED)
		EXPECT_NE(StringIdDebug::Lookup(meta.name).find("Position"), std::string_view::npos);
}

TEST(ComponentType, ConcurrentRegistrationGivesUniqueIds)
{
	constexpr int                   kThreads = 8;
	std::array<ComponentId, kThreads> ids{};
	std::latch                      start(kThreads);

	auto registerOne = [&]<int N>(std::integral_constant<int, N>)
	{
		start.arrive_and_wait();
		ids[N] = GetComponentId<ThreadComponent<N>>();
	};

	std::vector<std::thread> threads;
	[&]<int... Ns>(std::integer_sequence<int, Ns...>)
	{
		(threads.emplace_back(registerOne, std::integral_constant<int, Ns>{}), ...);
	}(std::make_integer_sequence<int, kThreads>{});
	for (std::thread& thread : threads)
		thread.join();

	const std::set<ComponentId> unique(ids.begin(), ids.end());
	EXPECT_EQ(unique.size(), static_cast<std::size_t>(kThreads));
}
