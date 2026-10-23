#pragma once

#include "Core/TypeList.h"
#include "ECS/ComponentConcepts.h"

#include <cstddef>
#include <type_traits>

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

// What a term means for a query: component, column type, callback argument, filter role.
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

// True if no passed term is written: every passed term is const T or Optional<const T>.
template <typename... Terms>
inline constexpr bool IsReadOnlyQuery = ((!TermTraits<Terms>::bPassed || std::is_const_v<typename TermTraits<Terms>::Column>) && ...);

namespace QueryTerms_Private
{
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

template <typename Term>
using PassedOnly = std::conditional_t<TermTraits<Term>::bPassed, TypeList<Term>, TypeList<>>;
} // namespace QueryTerms_Private
