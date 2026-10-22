// World structural costs (ADR-0004 step 9), compared with the EntityManager baseline in the same run.
// Run in Release only, on request or for critical changes.

#include "ECS/World.h"

#include <benchmark/benchmark.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace
{
template <int Tag>
struct WorldBenchData
{
	float x;
	float y;
};

template <int Base, std::size_t... Index>
void AddPoolTypes(World& world, EntityId owner, std::index_sequence<Index...>)
{
	(world.Add(owner, WorldBenchData<Base + static_cast<int>(Index)>{}), ...);
}
} // namespace

// CreateEntity + one Add per entity (baseline: CreateEntities).
static void World_CreateEntities(benchmark::State& state)
{
	World      world;
	const auto count = state.range(0);
	for (auto _ : state)
	{
		for (std::int64_t i = 0; i < count; ++i)
			world.Add(world.CreateEntity(), WorldBenchData<0>{});

		state.PauseTiming();
		world.Clear();
		state.ResumeTiming();
	}
	state.SetItemsProcessed(state.iterations() * count);
}

// DestroyEntity with 17 archetypes around (baseline: DestroyEntities).
static void World_DestroyEntities(benchmark::State& state)
{
	World          world;
	const EntityId owner = world.CreateEntity();
	AddPoolTypes<100>(world, owner, std::make_index_sequence<16>{});

	const auto            count = state.range(0);
	std::vector<EntityId> ids;
	ids.reserve(static_cast<std::size_t>(count));
	for (auto _ : state)
	{
		state.PauseTiming();
		ids.clear();
		for (std::int64_t i = 0; i < count; ++i)
		{
			ids.push_back(world.CreateEntity());
			world.Add(ids.back(), WorldBenchData<1>{});
		}
		state.ResumeTiming();

		for (const EntityId entity : ids)
			world.DestroyEntity(entity);
	}
	state.SetItemsProcessed(state.iterations() * count);
}

// Add then Remove one component on every entity: two archetype moves (baseline: AddRemoveChurn).
static void World_ArchetypeMove(benchmark::State& state)
{
	World                 world;
	std::vector<EntityId> ids;
	for (std::int64_t i = 0; i < state.range(0); ++i)
	{
		ids.push_back(world.CreateEntity());
		world.Add(ids.back(), WorldBenchData<2>{});
	}

	for (auto _ : state)
	{
		for (const EntityId entity : ids)
			world.Add(entity, WorldBenchData<3>{});
		for (const EntityId entity : ids)
			world.Remove<WorldBenchData<3>>(entity);
	}
	state.SetItemsProcessed(state.iterations() * state.range(0) * 2);
}

// Get<T> through the entity index (baseline: GetPool).
static void World_GetComponent(benchmark::State& state)
{
	World          world;
	const EntityId owner = world.CreateEntity();
	AddPoolTypes<200>(world, owner, std::make_index_sequence<16>{});

	for (auto _ : state)
		benchmark::DoNotOptimize(world.TryGet<WorldBenchData<207>>(owner));
}

BENCHMARK(World_CreateEntities)->Arg(1'000)->Arg(10'000)->Arg(100'000)->Unit(benchmark::kMicrosecond);
BENCHMARK(World_DestroyEntities)->Arg(1'000)->Arg(10'000)->Arg(100'000)->Unit(benchmark::kMicrosecond);
BENCHMARK(World_ArchetypeMove)->Arg(1'000)->Arg(10'000)->Arg(100'000)->Unit(benchmark::kMicrosecond);
BENCHMARK(World_GetComponent);
