#pragma once

#include "Core/Assert.h"
#include "Core/TypeList.h"
#include "ECS/Archetype.h"
#include "ECS/ChunkView.h"
#include "ECS/EntityId.h"
#include "ECS/QueryCache.h"
#include "ECS/QueryTerms.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace Query_Private
{
// RAII counter of active iterations on a World, used by the debug check "no structural change
// while iterating". Compiled only when MYRIAS_ASSERTS_ENABLED: in Release it does nothing.
#if MYRIAS_ASSERTS_ENABLED
class IterationScope
{
public:
	explicit IterationScope(std::atomic<std::uint32_t>& counter) noexcept : counter(counter)
	{
		counter.fetch_add(1, std::memory_order_relaxed);
	}
	~IterationScope() noexcept { counter.fetch_sub(1, std::memory_order_relaxed); }
	IterationScope(const IterationScope&)            = delete;
	IterationScope& operator=(const IterationScope&) = delete;

private:
	std::atomic<std::uint32_t>& counter;
};
#else
class IterationScope
{
public:
	explicit IterationScope(std::atomic<std::uint32_t>&) noexcept {}
};
#endif
} // namespace Query_Private

// Lightweight view over a World's cached query: owns nothing, stays valid as long as the World
// and sees archetypes created later (the cache is updated eagerly).
// Iteration order: archetype, chunk, row. Structural changes in the callback assert (Debug).
template <typename... Terms>
class Query
{
	static_assert(sizeof...(Terms) > 0, "a query needs at least one term");
	static_assert(HasNoDuplicates<typename TermTraits<Terms>::Component...>::value,
	              "a component appears in more than one query term (use it once: T, const T, Optional<T>, With<T> or Without<T>)");
	static constexpr std::size_t kPassed = QueryTerms_Private::kPassedCount<Terms...>;
	using PassedTerms = typename ConcatTypeLists<QueryTerms_Private::PassedOnly<Terms>...>::Type;

public:
	Query(const ECS_Private::QueryCache& cache, std::atomic<std::uint32_t>& activeIterations) noexcept
	    : cache(&cache)
	    , activeIterations(&activeIterations)
	{
	}

	// callback([EntityId,] arguments of the passed terms...)
	template <typename Callback>
	void ForEach(Callback&& callback) const
	{
		ForEachImpl(callback, PassedTerms{}, std::make_index_sequence<kPassed>{});
	}

	// callback(const ChunkView<Terms...>&)
	template <typename Callback>
	void ForEachChunk(Callback&& callback) const
	{
		const Query_Private::IterationScope scope(*activeIterations);
		VisitChunks([&](const ChunkView<Terms...>& view) { callback(view); });
	}

	[[nodiscard]] std::size_t EntityCount() const noexcept
	{
		std::size_t count = 0;
		for (const ECS_Private::Archetype* archetype : cache->archetypes)
			count += archetype->EntityCount();
		return count;
	}

	[[nodiscard]] std::size_t ChunkCount() const noexcept
	{
		std::size_t count = 0;
		for (const ECS_Private::Archetype* archetype : cache->archetypes)
			count += archetype->Chunks().size();
		return count;
	}

private:
	template <typename Visitor>
	void VisitChunks(Visitor&& visit) const
	{
		MYRIAS_ASSERT(cache->columns.size() == cache->archetypes.size() * kPassed,
		              "Query: cache columns out of sync with its archetypes");
		for (std::size_t a = 0; a < cache->archetypes.size(); ++a)
		{
			ECS_Private::Archetype& archetype = *cache->archetypes[a];
			if (archetype.EntityCount() == 0)
				continue;
			const int* const resolved = cache->columns.data() + a * kPassed;
			const auto       chunkCount = static_cast<std::uint32_t>(archetype.Chunks().size());
			for (std::uint32_t chunk = 0; chunk < chunkCount; ++chunk)
			{
				std::array<std::byte*, kPassed> columns{};
				for (std::size_t k = 0; k < kPassed; ++k)
					columns[k] = resolved[k] >= 0 ? static_cast<std::byte*>(archetype.ColumnData(chunk, resolved[k])) : nullptr;
				visit(ChunkView<Terms...>(columns, archetype.Entities(chunk)));
			}
		}
	}

	template <typename Callback, typename... Passed, std::size_t... Index>
	void ForEachImpl(Callback& callback, TypeList<Passed...>, std::index_sequence<Index...>) const
	{
		constexpr bool bWithEntity = std::is_invocable_v<Callback&, EntityId, typename TermTraits<Passed>::Argument...>;
		constexpr bool bWithout    = std::is_invocable_v<Callback&, typename TermTraits<Passed>::Argument...>;
		static_assert(bWithEntity || bWithout,
		              "ForEach callback does not match: expected ([EntityId,] T&, const T&, Optional<T> -> T*...)");

		const Query_Private::IterationScope scope(*activeIterations);
		VisitChunks(
		    [&](const ChunkView<Terms...>& view)
		    {
			    const std::span<const EntityId> entities = view.Entities();
			    for (std::size_t row = 0; row < entities.size(); ++row)
			    {
				    if constexpr (bWithEntity)
					    callback(entities[row], QueryTerms_Private::Fetch<Passed>(view.RawColumn(Index), row)...);
				    else
					    callback(QueryTerms_Private::Fetch<Passed>(view.RawColumn(Index), row)...);
			    }
		    });
	}

	const ECS_Private::QueryCache*                         cache;
	[[maybe_unused]] std::atomic<std::uint32_t>* activeIterations; // only used by the Debug guard
};
