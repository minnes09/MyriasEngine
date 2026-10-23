#pragma once

#include <type_traits>

// Opt-in for "cold" components that are not trivially copyable (e.g. hold a std::string).
// Specialise it next to the component, with a comment explaining why the type is not hot:
//   template <> inline constexpr bool IsColdComponent<MyType> = true;
template <typename T>
inline constexpr bool IsColdComponent = false;

// Hot components are plain data: trivially copyable, moved with memcpy between chunks.
// Cold components: default/copy constructors must not throw (except out of memory, which terminates), see ADR-0005.
template <typename T>
concept IsComponent = std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>
                   && std::is_nothrow_move_constructible_v<T> && std::is_nothrow_destructible_v<T>
                   && std::is_default_constructible_v<T>
                   && (std::is_trivially_copyable_v<T> || IsColdComponent<T>);
