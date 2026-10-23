// ChunkAllocator steady-state cost (ADR-0004 step 6, target <= 20 ns per Allocate+Free pair).
// Run in Release only, on request or for critical changes.

#include "ECS/Chunk.h"
#include "ECS/ChunkAllocator.h"

#include <benchmark/benchmark.h>

using namespace ECS_Private;

static void ChunkAllocateFree(benchmark::State& state)
{
	ChunkAllocator allocator;
	allocator.Reserve(ChunkAllocator::kChunksPerBlock);

	for (auto _ : state)
	{
		Chunk* const chunk = allocator.Allocate();
		benchmark::DoNotOptimize(chunk);
		allocator.Free(chunk);
	}
}

BENCHMARK(ChunkAllocateFree);
