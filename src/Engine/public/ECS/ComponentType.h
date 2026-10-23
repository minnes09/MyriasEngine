#pragma once

#include "ECS/ChunkLayout.h"
#include "ECS/ComponentConcepts.h"
#include "ECS/ComponentId.h"
#include "ECS/ComponentMeta.h"

#include <type_traits>

namespace ComponentType_Private
{
// Thread-safe. Registers the type in the process-wide registry (internal to the ECS module).
// Aborts (also in Release) when kMaxComponentTypes is exceeded.
[[nodiscard]] ComponentId Register(const ComponentMeta& meta) noexcept;

// One instantiation (and one static) per unqualified type.
template <typename Type>
[[nodiscard]] ComponentId RegisteredId() noexcept
{
	static const ComponentId id = Register(ComponentMeta_Private::MakeMeta<Type>());
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

// Meta of a registered component type. The reference stays valid: the table is append-only.
[[nodiscard]] const ComponentMeta& GetComponentMeta(ComponentId id) noexcept;

template <typename... Ts>
[[nodiscard]] ComponentMask MakeComponentMask() noexcept
{
	ComponentMask mask;
	(mask.set(GetComponentId<Ts>()), ...);
	return mask;
}
