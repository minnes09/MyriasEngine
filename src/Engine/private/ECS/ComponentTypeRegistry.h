#pragma once

#include "ECS/ComponentId.h"
#include "ECS/ComponentMeta.h"

#include <cstddef>

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
