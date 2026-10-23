#include "ECS/ChunkAllocator.h"

#include "Core/Assert.h"
#include "ECS/Chunk.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <functional>
#include <memory>
#include <numeric>
#include <vector>

namespace ECS_Private
{
namespace ChunkAllocator_Private
{
namespace
{
// The free list threads through the free chunks themselves: memcpy keeps it free of aliasing issues.
Chunk* Next(const Chunk* chunk) noexcept
{
	Chunk* next = nullptr;
	std::memcpy(&next, chunk->bytes, sizeof(next));
	return next;
}

void SetNext(Chunk* chunk, Chunk* next) noexcept
{
	std::memcpy(chunk->bytes, &next, sizeof(next));
}
} // namespace
} // namespace ChunkAllocator_Private

ChunkAllocator::ChunkAllocator() noexcept = default;
ChunkAllocator::~ChunkAllocator() noexcept = default;

Chunk* ChunkAllocator::Allocate() noexcept
{
	if (!freeList)
		AddBlock();

	Chunk* chunk = freeList;
	freeList     = ChunkAllocator_Private::Next(chunk);
	++allocatedCount;
	return chunk;
}

void ChunkAllocator::Free(Chunk* chunk) noexcept
{
	MYRIAS_ASSERT(chunk != nullptr, "ChunkAllocator::Free: null chunk");
	MYRIAS_ASSERT(Owns(chunk), "ChunkAllocator::Free: chunk comes from another allocator");
	MYRIAS_ASSERT(allocatedCount > 0, "ChunkAllocator::Free: more frees than allocations");

	ChunkAllocator_Private::SetNext(chunk, freeList);
	freeList = chunk;
	--allocatedCount;
}

void ChunkAllocator::Reserve(std::size_t chunkCount) noexcept
{
	blocks.reserve((chunkCount + kChunksPerBlock - 1) / kChunksPerBlock);
	while (ReservedChunkCount() < chunkCount)
		AddBlock();
}

void ChunkAllocator::Trim() noexcept
{
	const std::size_t blockCount = blocks.size();
	if (blockCount == 0)
		return;

	// Block indices sorted by address, to find the block of a free chunk by binary search.
	std::vector<std::size_t> byAddress(blockCount);
	std::iota(byAddress.begin(), byAddress.end(), std::size_t{ 0 });
	const std::less<const Chunk*> before;
	std::ranges::sort(byAddress, [&](std::size_t a, std::size_t b) { return before(blocks[a].get(), blocks[b].get()); });

	auto blockOf = [&](const Chunk* chunk)
	{
		const auto it = std::ranges::upper_bound(byAddress, chunk, before,
		                                         [&](std::size_t index) { return blocks[index].get(); });
		return *(it - 1);
	};

	std::vector<std::size_t> freeCounts(blockCount, 0);
	for (const Chunk* chunk = freeList; chunk; chunk = ChunkAllocator_Private::Next(chunk))
		++freeCounts[blockOf(chunk)];

	// Rebuild the free list without the chunks of the blocks about to be released.
	Chunk* kept = nullptr;
	for (Chunk* chunk = freeList; chunk;)
	{
		Chunk* const next = ChunkAllocator_Private::Next(chunk);
		if (freeCounts[blockOf(chunk)] < kChunksPerBlock)
		{
			ChunkAllocator_Private::SetNext(chunk, kept);
			kept = chunk;
		}
		chunk = next;
	}
	freeList = kept;

	std::vector<std::unique_ptr<Chunk[]>> keptBlocks;
	keptBlocks.reserve(blockCount);
	for (std::size_t i = 0; i < blockCount; ++i)
		if (freeCounts[i] < kChunksPerBlock)
			keptBlocks.push_back(std::move(blocks[i]));
	blocks = std::move(keptBlocks);
}

void ChunkAllocator::AddBlock() noexcept
{
	blocks.push_back(std::make_unique_for_overwrite<Chunk[]>(kChunksPerBlock));
	Chunk* const block = blocks.back().get();

	// Pushed in reverse so the block is handed out in ascending address order.
	for (std::size_t i = kChunksPerBlock; i-- > 0;)
	{
		ChunkAllocator_Private::SetNext(block + i, freeList);
		freeList = block + i;
	}
}

bool ChunkAllocator::Owns(const Chunk* chunk) const noexcept
{
	const std::less_equal<const Chunk*> notAfter;
	const std::less<const Chunk*>       before;
	return std::ranges::any_of(blocks,
	                           [&](const std::unique_ptr<Chunk[]>& block)
	                           { return notAfter(block.get(), chunk) && before(chunk, block.get() + kChunksPerBlock); });
}
} // namespace ECS_Private
