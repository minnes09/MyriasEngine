#pragma once

#include <cstddef>
#include <memory>
#include <vector>

struct Chunk;

namespace ECS_Private
{
// Hands out Chunks, one allocator per World. Memory comes in blocks of kChunksPerBlock chunks
// (1 MiB); freed chunks go to an intrusive free list and are reused most-recent-first, so after
// warm-up Allocate/Free never touch the heap. Not thread-safe.
class ChunkAllocator
{
public:
	static constexpr std::size_t kChunksPerBlock = 64;

	ChunkAllocator() noexcept;
	~ChunkAllocator() noexcept;
	ChunkAllocator(const ChunkAllocator&)            = delete;
	ChunkAllocator& operator=(const ChunkAllocator&) = delete;
	ChunkAllocator(ChunkAllocator&&)            = delete;
	ChunkAllocator& operator=(ChunkAllocator&&) = delete;

	// Uninitialised chunk: a recycled one if available, otherwise from a new block.
	[[nodiscard]] Chunk* Allocate() noexcept;
	// Returns the chunk to the free list; its memory stays reserved.
	void Free(Chunk* chunk) noexcept;

	// Pre-allocates blocks until at least chunkCount chunks are reserved.
	void Reserve(std::size_t chunkCount) noexcept;
	// Releases the blocks whose chunks are all free. Maintenance only: may allocate, never per frame.
	void Trim() noexcept;

	[[nodiscard]] std::size_t AllocatedChunkCount() const noexcept { return allocatedCount; }
	[[nodiscard]] std::size_t ReservedChunkCount() const noexcept { return blocks.size() * kChunksPerBlock; }

private:
	void               AddBlock() noexcept;
	[[nodiscard]] bool Owns(const Chunk* chunk) const noexcept;

	std::vector<std::unique_ptr<Chunk[]>> blocks;
	Chunk*                                freeList       = nullptr; // next pointer stored in the chunk bytes
	std::size_t                           allocatedCount = 0;
};
} // namespace ECS_Private
