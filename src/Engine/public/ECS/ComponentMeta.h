#pragma once

#include "Core/StringId.h"
#include "Core/TypeName.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

// Type-erased description of a component type, used by archetype columns on structural paths
// only (never per entity in the frame). Null function pointers mean "trivial": the caller uses
// memcpy (move/copy), does nothing (destroy) or leaves memory as is (default construct).
struct ComponentMeta
{
	std::uint32_t size      = 0;
	std::uint32_t alignment = 0;
	StringId      name; // from the type name: diagnostics (compiler-specific spelling)
	bool          bTriviallyCopyable = true;
	bool          bTag               = false; // empty type: part of the signature, no column

	void (*defaultConstruct)(void* destination, std::size_t count) noexcept                  = nullptr;
	void (*moveConstruct)(void* destination, void* source, std::size_t count) noexcept       = nullptr;
	void (*copyConstruct)(void* destination, const void* source, std::size_t count) noexcept = nullptr;
	void (*destroy)(void* destination, std::size_t count) noexcept                           = nullptr;
};

namespace ComponentMeta_Private
{
template <typename T>
void DefaultConstruct(void* destination, std::size_t count) noexcept
{
	T* typed = static_cast<T*>(destination);
	for (std::size_t i = 0; i < count; ++i)
		std::construct_at(typed + i);
}

template <typename T>
void MoveConstruct(void* destination, void* source, std::size_t count) noexcept
{
	T* to   = static_cast<T*>(destination);
	T* from = static_cast<T*>(source);
	for (std::size_t i = 0; i < count; ++i)
		std::construct_at(to + i, std::move(from[i]));
}

template <typename T>
void CopyConstruct(void* destination, const void* source, std::size_t count) noexcept
{
	T*       to   = static_cast<T*>(destination);
	const T* from = static_cast<const T*>(source);
	for (std::size_t i = 0; i < count; ++i)
		std::construct_at(to + i, from[i]);
}

template <typename T>
void Destroy(void* destination, std::size_t count) noexcept
{
	std::destroy_n(static_cast<T*>(destination), count);
}

template <typename T>
[[nodiscard]] ComponentMeta MakeMeta() noexcept
{
	ComponentMeta meta;
	meta.size               = static_cast<std::uint32_t>(sizeof(T));
	meta.alignment          = static_cast<std::uint32_t>(alignof(T));
	meta.name               = StringId{ TypeName<T>() };
	meta.bTriviallyCopyable = std::is_trivially_copyable_v<T>;
	meta.bTag               = std::is_empty_v<T>;
	if constexpr (!std::is_trivially_default_constructible_v<T>)
		meta.defaultConstruct = &DefaultConstruct<T>;
	if constexpr (!std::is_trivially_copyable_v<T>)
	{
		meta.moveConstruct = &MoveConstruct<T>;
		meta.copyConstruct = &CopyConstruct<T>;
		meta.destroy       = &Destroy<T>;
	}
	return meta;
}
} // namespace ComponentMeta_Private
