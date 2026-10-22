#pragma once

#include "Core/Assert.h"
#include "Core/StringId.h"
#include "ECS/Chunk.h"
#include "ECS/EntityId.h"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <source_location>
#include <string_view>
#include <type_traits>
#include <utility>

// Index of a component type in per-type arrays. Process-wide: the same in every World.
// Never serialize it (it depends on registration order); use ComponentMeta::name instead.
using ComponentId = std::uint16_t;

inline constexpr std::size_t kMaxComponentTypes = 512;

// One bit per component type: archetype signatures, query filters, system access sets.
using ComponentMask = std::bitset<kMaxComponentTypes>;

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

// Process-wide, append-only table of component types. Entries are written once at registration
// and never change, so reads need no lock and every World shares the same ids.
class ComponentTypeRegistry
{
public:
	// Thread-safe. Aborts (also in Release) when kMaxComponentTypes is exceeded.
	[[nodiscard]] static ComponentId          Register(const ComponentMeta& meta) noexcept;
	[[nodiscard]] static const ComponentMeta& Get(ComponentId id) noexcept;
	// Number of ids handed out so far.
	[[nodiscard]] static std::size_t          Count() noexcept;
};

namespace ComponentType_Private
{
template <typename T>
[[nodiscard]] constexpr std::string_view RawTypeName() noexcept
{
	return std::source_location::current().function_name();
}

// Extracts T from the compiler's function signature, e.g.
//   MSVC:  "... RawTypeName<struct Foo>(void)"   Clang: "... RawTypeName() [T = Foo]"
template <typename T>
[[nodiscard]] constexpr std::string_view TypeName() noexcept
{
	std::string_view name = RawTypeName<T>();
	if (const auto start = name.find("[T = "); start != std::string_view::npos)
	{
		name.remove_prefix(start + 5);
		name = name.substr(0, name.rfind(']'));
	}
	else if (const auto open = name.find("RawTypeName<"); open != std::string_view::npos)
	{
		name.remove_prefix(open + 12);
		name = name.substr(0, name.rfind(">("));
	}
	for (const std::string_view prefix : { std::string_view("struct "), std::string_view("class ") })
		if (name.starts_with(prefix))
			name.remove_prefix(prefix.size());
	return name;
}

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

// One instantiation (and one static) per unqualified type.
template <typename Type>
[[nodiscard]] ComponentId RegisteredId() noexcept
{
	static const ComponentId id = ComponentTypeRegistry::Register(MakeMeta<Type>());
	return id;
}
} // namespace ComponentType_Private

// Id of component type T (const T and T share it). Registered on first use; later calls read
// a cached value.
template <typename T>
[[nodiscard]] ComponentId GetComponentId() noexcept
{
	using Type = std::remove_cv_t<T>;
	static_assert(IsComponent<Type>, "not a component: must be trivially copyable, or opt in with IsColdComponent");
	static_assert(alignof(Type) <= kChunkAlignment, "component alignment above kChunkAlignment");
	// A one-row chunk: the first column starts at kFirstColumnOffset (see Archetype::ComputeLayout).
	static_assert(kFirstColumnOffset + sizeof(Type) <= kChunkSize,
	              "component too big: a row must fit in a chunk");
	return ComponentType_Private::RegisteredId<Type>();
}

[[nodiscard]] inline const ComponentMeta& GetComponentMeta(ComponentId id) noexcept
{
	return ComponentTypeRegistry::Get(id);
}

template <typename... Ts>
[[nodiscard]] ComponentMask MakeComponentMask() noexcept
{
	ComponentMask mask;
	(mask.set(GetComponentId<Ts>()), ...);
	return mask;
}
