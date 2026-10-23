#pragma once

#include <type_traits>

template <typename...>
struct TypeList
{
};

template <typename... Lists>
struct ConcatTypeLists;
template <>
struct ConcatTypeLists<>
{
	using Type = TypeList<>;
};
template <typename... A>
struct ConcatTypeLists<TypeList<A...>>
{
	using Type = TypeList<A...>;
};
template <typename... A, typename... B, typename... Rest>
struct ConcatTypeLists<TypeList<A...>, TypeList<B...>, Rest...>
{
	using Type = typename ConcatTypeLists<TypeList<A..., B...>, Rest...>::Type;
};

// True if every type is different from all the others.
template <typename...>
struct HasNoDuplicates : std::true_type
{
};
template <typename T, typename... Rest>
struct HasNoDuplicates<T, Rest...>
    : std::bool_constant<(!std::is_same_v<T, Rest> && ...) && HasNoDuplicates<Rest...>::value>
{
};
