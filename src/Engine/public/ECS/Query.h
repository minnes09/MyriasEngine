#pragma once

#include "Core/Assert.h"
#include "ECS/Archetype.h"
#include "ECS/ComponentType.h"
#include "ECS/EntityId.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

// Query terms. Passed terms (they become callback arguments):
//   T           required, written:  T&
//   const T     required, read:     const T&
//   Optional<T> may be absent:      T*  (null when the archetype has no T); also Optional<const T>
// Filter-only terms (no argument):
//   With<T>     required
//   Without<T>  excluded
// A tag (empty type) cannot be a passed term: use With<Tag> / Without<Tag>.
template <typename T>
struct Optional
{
};

template <typename T>
struct With
{
};

template <typename T>
struct Without
{
};

// Process-wide id of a query type (a list of terms), used to index each World's QueryCache table.
using QueryId = std::uint32_t;

// Capacity of every World's query table (one slot per query type used in the process; the table
// costs sizeof(void*) per slot per World). Exceeding it aborts. Change it here only.
inline constexpr std::size_t kMaxQueryTypes = 1024;

namespace Query_Private
{
[[nodiscard]] QueryId AllocateQueryId() noexcept;

template <typename...>
struct TypeList
{
};

template <typename Term>
struct TermTraits
{
	using Component = std::remove_cv_t<Term>;
	using Column    = Term;
	using Argument  = Term&;

	static constexpr bool bPassed   = true;
	static constexpr bool bOptional = false;
	static constexpr bool bRequired = true;
	static constexpr bool bExcluded = false;

	static_assert(IsComponent<Component>, "query term is not a component (use T, const T, Optional<T>, With<T>, Without<T>)");
	static_assert(!std::is_empty_v<Component>, "a tag cannot be a passed term: use With<Tag> or Without<Tag>");
};

template <typename T>
struct TermTraits<Optional<T>>
{
	using Component = std::remove_cv_t<T>;
	using Column    = T;
	using Argument  = T*;

	static constexpr bool bPassed   = true;
	static constexpr bool bOptional = true;
	static constexpr bool bRequired = false;
	static constexpr bool bExcluded = false;

	static_assert(IsComponent<Component>, "Optional<T>: T is not a component");
	static_assert(!std::is_empty_v<Component>, "a tag cannot be a passed term: use With<Tag> or Without<Tag>");
};

template <typename T>
struct TermTraits<With<T>>
{
	using Component = T;
	using Column    = void;
	using Argument  = void;

	static constexpr bool bPassed   = false;
	static constexpr bool bOptional = false;
	static constexpr bool bRequired = true;
	static constexpr bool bExcluded = false;

	static_assert(IsComponent<T>, "With<T>: T is not a component");
};

template <typename T>
struct TermTraits<Without<T>>
{
	using Component = T;
	using Column    = void;
	using Argument  = void;

	static constexpr bool bPassed   = false;
	static constexpr bool bOptional = false;
	static constexpr bool bRequired = false;
	static constexpr bool bExcluded = true;

	static_assert(IsComponent<T>, "Without<T>: T is not a component");
};

template <typename... Terms>
inline constexpr std::size_t kPassedCount = (std::size_t{ TermTraits<Terms>::bPassed } + ... + std::size_t{ 0 });

// Position, among the passed terms, of the one whose column type is ColumnType.
template <typename ColumnType, typename... Terms>
[[nodiscard]] consteval std::size_t PassedIndex() noexcept
{
	constexpr bool passed[]  = { TermTraits<Terms>::bPassed... };
	constexpr bool matches[] = { std::is_same_v<typename TermTraits<Terms>::Column, ColumnType>... };
	std::size_t    index     = 0;
	for (std::size_t i = 0; i < sizeof...(Terms); ++i)
	{
		if (!passed[i])
			continue;
		if (matches[i])
			return index;
		++index;
	}
	return kPassedCount<Terms...>; // not found: callers static_assert on it
}

// Argument of the callback for one passed term, at the given row.
template <typename Term>
[[nodiscard]] typename TermTraits<Term>::Argument Fetch(std::byte* column, std::size_t row) noexcept
{
	using Traits = TermTraits<Term>;
	using Column = typename Traits::Column;
	if constexpr (Traits::bOptional)
		return column ? static_cast<Column*>(static_cast<void*>(column)) + row : nullptr;
	else
		return static_cast<Column*>(static_cast<void*>(column))[row];
}

template <typename... Lists>
struct ConcatLists;
template <>
struct ConcatLists<>
{
	using Type = TypeList<>;
};
template <typename... A>
struct ConcatLists<TypeList<A...>>
{
	using Type = TypeList<A...>;
};
template <typename... A, typename... B, typename... Rest>
struct ConcatLists<TypeList<A...>, TypeList<B...>, Rest...>
{
	using Type = typename ConcatLists<TypeList<A..., B...>, Rest...>::Type;
};

template <typename Term>
using PassedOnly = std::conditional_t<TermTraits<Term>::bPassed, TypeList<Term>, TypeList<>>;

// True if every type is different from all the others.
template <typename...>
struct AllDistinct : std::true_type
{
};
template <typename T, typename... Rest>
struct AllDistinct<T, Rest...>
    : std::bool_constant<(!std::is_same_v<T, Rest> && ...) && AllDistinct<Rest...>::value>
{
};

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

template <typename... Terms>
[[nodiscard]] QueryId GetQueryId() noexcept
{
	static const QueryId id = Query_Private::AllocateQueryId();
	return id;
}

// Per-World, per-query-type cache: the archetypes that match, with the column of every passed
// term already resolved. Internal to World/Query. Updated eagerly by the World when it creates an
// archetype (structural, single-thread), so reading it never writes: concurrent reads are safe.
struct QueryCache
{
	ComponentMask            required;
	ComponentMask            excluded;
	std::vector<ComponentId> passed;            // ComponentId of every passed term, in term order
	std::vector<Archetype*>  archetypes;        // matching archetypes, in creation order
	std::vector<int>         columns;           // archetypes.size() * passed.size(): column index, -1 if absent
};

template <typename... Terms>
class Query;

using QueryCacheUnique = std::unique_ptr<QueryCache>;

// One chunk of a matching archetype, as seen by ForEachChunk: contiguous columns of the passed terms.
template <typename... Terms>
class ChunkView
{
	static constexpr std::size_t kPassed = Query_Private::kPassedCount<Terms...>;

public:
	ChunkView(const std::array<std::byte*, kPassed>& columns, std::span<const EntityId> entities) noexcept
	    : columns(columns)
	    , entities(entities)
	{
	}

	[[nodiscard]] std::size_t               Count() const noexcept { return entities.size(); }
	[[nodiscard]] std::span<const EntityId> Entities() const noexcept { return entities; }

	// Column of the term whose component type is ColumnType (T, const T; also for Optional<T>).
	// Empty for an Optional term absent from the archetype.
	template <typename ColumnType>
	[[nodiscard]] std::span<ColumnType> Column() const noexcept
	{
		constexpr std::size_t index = Query_Private::PassedIndex<ColumnType, Terms...>();
		static_assert(index < kPassed, "ChunkView::Column: no such passed term (use the exact T or const T of the query)");
		std::byte* const column = columns[index];
		return { column ? static_cast<ColumnType*>(static_cast<void*>(column)) : nullptr, column ? entities.size() : 0 };
	}

private:
	template <typename...>
	friend class Query;

	// Start of the column of the Index-th passed term (null for an absent Optional).
	[[nodiscard]] std::byte* RawColumn(std::size_t index) const noexcept { return columns[index]; }

	std::array<std::byte*, kPassed> columns;
	std::span<const EntityId>       entities;
};

// Lightweight view over a World's cached query: owns nothing, stays valid as long as the World
// and sees archetypes created later (the cache is updated eagerly).
// Iteration order: archetype, chunk, row. Structural changes in the callback assert (Debug).
template <typename... Terms>
class Query
{
	static_assert(sizeof...(Terms) > 0, "a query needs at least one term");
	static_assert(Query_Private::AllDistinct<typename Query_Private::TermTraits<Terms>::Component...>::value,
	              "a component appears in more than one query term (use it once: T, const T, Optional<T>, With<T> or Without<T>)");
	static constexpr std::size_t kPassed = Query_Private::kPassedCount<Terms...>;
	using PassedTerms = typename Query_Private::ConcatLists<Query_Private::PassedOnly<Terms>...>::Type;

public:
	Query(const QueryCache& cache, std::atomic<std::uint32_t>& activeIterations) noexcept
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
		for (const Archetype* archetype : cache->archetypes)
			count += archetype->EntityCount();
		return count;
	}

	[[nodiscard]] std::size_t ChunkCount() const noexcept
	{
		std::size_t count = 0;
		for (const Archetype* archetype : cache->archetypes)
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
			Archetype& archetype = *cache->archetypes[a];
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
	void ForEachImpl(Callback& callback, Query_Private::TypeList<Passed...>, std::index_sequence<Index...>) const
	{
		constexpr bool bWithEntity = std::is_invocable_v<Callback&, EntityId, typename Query_Private::TermTraits<Passed>::Argument...>;
		constexpr bool bWithout    = std::is_invocable_v<Callback&, typename Query_Private::TermTraits<Passed>::Argument...>;
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
					    callback(entities[row], Query_Private::Fetch<Passed>(view.RawColumn(Index), row)...);
				    else
					    callback(Query_Private::Fetch<Passed>(view.RawColumn(Index), row)...);
			    }
		    });
	}

	const QueryCache*                         cache;
	[[maybe_unused]] std::atomic<std::uint32_t>* activeIterations; // only used by the Debug guard
};
