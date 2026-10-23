#include "ECS/Chunk.h"
#include "ECS/ChunkAllocator.h"

#include "Support/AllocationCounter.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>

using namespace ECS_Private;

namespace
{
std::vector<Chunk*> AllocateMany(ChunkAllocator& allocator, std::size_t count)
{
	std::vector<Chunk*> chunks;
	for (std::size_t i = 0; i < count; ++i)
		chunks.push_back(allocator.Allocate());
	return chunks;
}

void FreeAll(ChunkAllocator& allocator, const std::vector<Chunk*>& chunks)
{
	for (Chunk* chunk : chunks)
		allocator.Free(chunk);
}
} // namespace

TEST(ChunkAllocator, ChunksAreAlignedDistinctAndFullyWritable)
{
	ChunkAllocator            allocator;
	const std::vector<Chunk*> chunks = AllocateMany(allocator, 3);

	for (Chunk* chunk : chunks)
		EXPECT_EQ(reinterpret_cast<std::uintptr_t>(chunk) % kChunkAlignment, 0u);

	// Fill each chunk with its own byte, then check none overwrote another.
	for (std::size_t i = 0; i < chunks.size(); ++i)
		std::memset(chunks[i]->bytes, static_cast<int>(i + 1), kChunkSize);
	for (std::size_t i = 0; i < chunks.size(); ++i)
	{
		EXPECT_EQ(chunks[i]->bytes[0], std::byte(i + 1));
		EXPECT_EQ(chunks[i]->bytes[kChunkSize - 1], std::byte(i + 1));
	}

	FreeAll(allocator, chunks);
}

TEST(ChunkAllocator, FreedChunkIsReusedFirst)
{
	ChunkAllocator allocator;
	Chunk* const   first  = allocator.Allocate();
	Chunk* const   second = allocator.Allocate();

	allocator.Free(first);
	EXPECT_EQ(allocator.Allocate(), first);

	allocator.Free(first);
	allocator.Free(second);
}

TEST(ChunkAllocator, CountsFollowBlocks)
{
	ChunkAllocator allocator;
	EXPECT_EQ(allocator.ReservedChunkCount(), 0u);

	const std::vector<Chunk*> chunks = AllocateMany(allocator, ChunkAllocator::kChunksPerBlock + 1);
	EXPECT_EQ(allocator.AllocatedChunkCount(), ChunkAllocator::kChunksPerBlock + 1);
	EXPECT_EQ(allocator.ReservedChunkCount(), 2 * ChunkAllocator::kChunksPerBlock);

	FreeAll(allocator, chunks);
	EXPECT_EQ(allocator.AllocatedChunkCount(), 0u);
	EXPECT_EQ(allocator.ReservedChunkCount(), 2 * ChunkAllocator::kChunksPerBlock);
}

TEST(ChunkAllocator, TrimReleasesOnlyFullyFreeBlocks)
{
	ChunkAllocator      allocator;
	std::vector<Chunk*> chunks = AllocateMany(allocator, ChunkAllocator::kChunksPerBlock + 1);

	// The last chunk is the only one from the second block: freeing it empties that block.
	allocator.Free(chunks.back());
	chunks.pop_back();
	allocator.Trim();
	EXPECT_EQ(allocator.ReservedChunkCount(), ChunkAllocator::kChunksPerBlock);

	// A partially used block is kept, and its live chunks stay valid.
	for (std::size_t i = 0; i < 10; ++i)
		allocator.Free(chunks[i]);
	std::memset(chunks[20]->bytes, 0x5A, kChunkSize);
	allocator.Trim();
	EXPECT_EQ(allocator.ReservedChunkCount(), ChunkAllocator::kChunksPerBlock);
	EXPECT_EQ(chunks[20]->bytes[kChunkSize - 1], std::byte{ 0x5A });

	// The free list still works after Trim.
	Chunk* const reused = allocator.Allocate();
	EXPECT_TRUE(std::ranges::find(chunks.begin(), chunks.begin() + 10, reused) != chunks.begin() + 10);

	allocator.Free(reused);
	FreeAll(allocator, std::vector<Chunk*>(chunks.begin() + 10, chunks.end()));
	allocator.Trim();
	EXPECT_EQ(allocator.ReservedChunkCount(), 0u);
}

TEST(ChunkAllocator, SteadyStateDoesNotAllocate)
{
	ChunkAllocator allocator;
	allocator.Reserve(128);
	std::vector<Chunk*> chunks;
	chunks.reserve(100);

	std::size_t count = 0;
	{
		const AllocationScope scope;
		for (int round = 0; round < 3; ++round)
		{
			for (int i = 0; i < 100; ++i)
				chunks.push_back(allocator.Allocate());
			for (Chunk* chunk : chunks)
				allocator.Free(chunk);
			chunks.clear();
		}
		count = scope.Count();
	}
	EXPECT_EQ(count, 0u);
}
