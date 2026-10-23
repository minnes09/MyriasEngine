#pragma once

#include "ECS/EntityId.h"
#include "ECS/QueryTerms.h"

#include <array>
#include <cstddef>
#include <span>

template <typename... Terms>
class Query;

// One chunk of a matching archetype, as seen by ForEachChunk: contiguous columns of the passed terms.
template <typename... Terms>
class ChunkView
{
	static constexpr std::size_t kPassed = QueryTerms_Private::kPassedCount<Terms...>;

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
		constexpr std::size_t index = QueryTerms_Private::PassedIndex<ColumnType, Terms...>();
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
