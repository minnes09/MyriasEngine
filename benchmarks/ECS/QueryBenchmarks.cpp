// Query iteration costs (ADR-0004 step 10). Run in Release only, on request or for critical changes.

#include "ECS/World.h"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{
struct QPosition
{
	float x = 0.f;
	float y = 0.f;
};

struct QVelocity
{
	float dx = 1.f;
	float dy = 1.f;
};

struct QAcceleration
{
	float ax = 0.5f;
	float ay = 0.5f;
};

template <int Index>
struct QTag
{
};

void Populate1(World& world, std::int64_t count)
{
	world.Reserve(static_cast<std::size_t>(count));
	for (std::int64_t i = 0; i < count; ++i)
		world.Add(world.CreateEntity(), QPosition{});
}

// Every entity has Position and Velocity.
void Populate2(World& world, std::int64_t count)
{
	world.Reserve(static_cast<std::size_t>(count));
	for (std::int64_t i = 0; i < count; ++i)
	{
		const EntityId entity = world.CreateEntity();
		world.Add(entity, QPosition{});
		world.Add(entity, QVelocity{});
	}
}

void Populate2Sparse(World& world, std::int64_t count)
{
	world.Reserve(static_cast<std::size_t>(count));
	for (std::int64_t i = 0; i < count; ++i)
	{
		const EntityId entity = world.CreateEntity();
		world.Add(entity, QPosition{});
		if (i % 10 == 0) // 10% have the second component
			world.Add(entity, QVelocity{});
	}
}

void Populate3(World& world, std::int64_t count)
{
	world.Reserve(static_cast<std::size_t>(count));
	for (std::int64_t i = 0; i < count; ++i)
	{
		const EntityId entity = world.CreateEntity();
		world.Add(entity, QPosition{});
		world.Add(entity, QVelocity{});
		world.Add(entity, QAcceleration{});
	}
}

template <std::size_t... Index>
void AddTags(World& world, EntityId entity, unsigned bits, std::index_sequence<Index...>)
{
	((bits & (1u << Index) ? static_cast<void>(world.Add(entity, QTag<static_cast<int>(Index)>{})) : void()), ...);
}

// 64 archetypes: Position plus every combination of 6 tags.
void PopulateFragmented(World& world, std::int64_t count)
{
	world.Reserve(static_cast<std::size_t>(count));
	for (std::int64_t i = 0; i < count; ++i)
	{
		const EntityId entity = world.CreateEntity();
		world.Add(entity, QPosition{});
		AddTags(world, entity, static_cast<unsigned>(i % 64), std::make_index_sequence<6>{});
	}
}
} // namespace

static void World_Iterate1(benchmark::State& state)
{
	World world;
	Populate1(world, state.range(0));
	for (auto _ : state)
	{
		world.Query<QPosition>().ForEach(
		    [](QPosition& p)
		    {
			    p.x += 1.f;
			    p.y += 1.f;
		    });
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * state.range(0));
}

static void World_IterateChunk1(benchmark::State& state)
{
	World world;
	Populate1(world, state.range(0));
	for (auto _ : state)
	{
		world.Query<QPosition>().ForEachChunk(
		    [](const ChunkView<QPosition>& chunk)
		    {
			    for (QPosition& p : chunk.Column<QPosition>())
			    {
				    p.x += 1.f;
				    p.y += 1.f;
			    }
		    });
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * state.range(0));
}

static void World_Join2(benchmark::State& state)
{
	World world;
	Populate2(world, state.range(0));
	for (auto _ : state)
	{
		world.Query<QPosition, const QVelocity>().ForEach(
		    [](QPosition& p, const QVelocity& v)
		    {
			    p.x += v.dx;
			    p.y += v.dy;
		    });
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * state.range(0));
}

static void World_Join2Chunk(benchmark::State& state)
{
	World world;
	Populate2(world, state.range(0));
	for (auto _ : state)
	{
		world.Query<QPosition, const QVelocity>().ForEachChunk(
		    [](const ChunkView<QPosition, const QVelocity>& chunk)
		    {
			    const std::span<QPosition>        positions  = chunk.Column<QPosition>();
			    const std::span<const QVelocity>  velocities = chunk.Column<const QVelocity>();
			    for (std::size_t i = 0; i < chunk.Count(); ++i)
			    {
				    positions[i].x += velocities[i].dx;
				    positions[i].y += velocities[i].dy;
			    }
		    });
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * state.range(0));
}

// Optional second component, present on 10% of the entities.
static void World_Join2Optional(benchmark::State& state)
{
	World world;
	Populate2Sparse(world, state.range(0));
	for (auto _ : state)
	{
		world.Query<QPosition, Optional<const QVelocity>>().ForEach(
		    [](QPosition& p, const QVelocity* v)
		    {
			    if (v)
			    {
				    p.x += v->dx;
				    p.y += v->dy;
			    }
		    });
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * state.range(0));
}

// Required join where only 10% of the entities (same population as Join2Optional) match.
static void World_Join2Sparse(benchmark::State& state)
{
	World world;
	Populate2Sparse(world, state.range(0));
	for (auto _ : state)
	{
		world.Query<QPosition, const QVelocity>().ForEach(
		    [](QPosition& p, const QVelocity& v)
		    {
			    p.x += v.dx;
			    p.y += v.dy;
		    });
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * state.range(0));
}

static void World_Join3(benchmark::State& state)
{
	World world;
	Populate3(world, state.range(0));
	for (auto _ : state)
	{
		world.Query<QPosition, QVelocity, const QAcceleration>().ForEach(
		    [](QPosition& p, QVelocity& v, const QAcceleration& a)
		    {
			    v.dx += a.ax;
			    v.dy += a.ay;
			    p.x += v.dx;
			    p.y += v.dy;
		    });
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * state.range(0));
}

// 64 archetypes of the same size class: cost of visiting many small archetypes.
static void World_JoinFragmented(benchmark::State& state)
{
	World world;
	PopulateFragmented(world, state.range(0));
	for (auto _ : state)
	{
		world.Query<QPosition>().ForEach(
		    [](QPosition& p)
		    {
			    p.x += 1.f;
			    p.y += 1.f;
		    });
		benchmark::ClobberMemory();
	}
	state.SetItemsProcessed(state.iterations() * state.range(0));
}

// Cost of world.Query<...>() alone once the cache is warm.
static void World_QueryLookup(benchmark::State& state)
{
	World world;
	Populate2(world, 100);
	benchmark::DoNotOptimize(world.Query<QPosition, const QVelocity>().EntityCount());
	for (auto _ : state)
	{
		const auto query = world.Query<QPosition, const QVelocity>();
		benchmark::DoNotOptimize(query);
	}
	state.SetItemsProcessed(state.iterations());
}

BENCHMARK(World_Iterate1)->Arg(1'000)->Arg(10'000)->Arg(100'000)->Unit(benchmark::kMicrosecond);
BENCHMARK(World_IterateChunk1)->Arg(1'000)->Arg(10'000)->Arg(100'000)->Arg(1'000'000)->Unit(benchmark::kMicrosecond);
BENCHMARK(World_Join2)->Arg(1'000)->Arg(10'000)->Arg(100'000)->Arg(1'000'000)->Unit(benchmark::kMicrosecond);
BENCHMARK(World_Join2Chunk)->Arg(1'000)->Arg(10'000)->Arg(100'000)->Arg(1'000'000)->Unit(benchmark::kMicrosecond);
BENCHMARK(World_Join2Optional)->Arg(1'000)->Arg(10'000)->Arg(100'000)->Unit(benchmark::kMicrosecond);
BENCHMARK(World_Join2Sparse)->Arg(1'000)->Arg(10'000)->Arg(100'000)->Unit(benchmark::kMicrosecond);
BENCHMARK(World_Join3)->Arg(1'000)->Arg(10'000)->Arg(100'000)->Unit(benchmark::kMicrosecond);
BENCHMARK(World_JoinFragmented)->Arg(1'000)->Arg(10'000)->Arg(100'000)->Unit(benchmark::kMicrosecond);
BENCHMARK(World_QueryLookup)->Unit(benchmark::kNanosecond);
