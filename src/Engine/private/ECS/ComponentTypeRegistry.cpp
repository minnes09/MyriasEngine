#include "ECS/ComponentTypeRegistry.h"

#include "Core/Assert.h"
#include "Core/Fatal.h"
#include "ECS/ComponentType.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace ComponentType_Private
{
namespace
{
// Constant-initialised: safe to use from static initialisers of other translation units.
// Same scheme as StringIdDebug and WorldIdAllocator: mutex on the rare write, lock-free reads.
constinit std::array<ComponentMeta, kMaxComponentTypes> metas{};
constinit std::atomic<std::uint32_t>                    count{ 0 }; // entries [0, count) are written and immutable
constinit std::mutex                                    registerMutex;
} // namespace
} // namespace ComponentType_Private

ComponentId ComponentTypeRegistry::Register(const ComponentMeta& meta) noexcept
{
	const std::scoped_lock lock(ComponentType_Private::registerMutex);

	const std::uint32_t index = ComponentType_Private::count.load(std::memory_order_relaxed);
	if (index >= kMaxComponentTypes) // always checked: writing past the table would corrupt memory
	{
		FatalError("ComponentTypeRegistry: too many component types: raise kMaxComponentTypes");
	}

	ComponentType_Private::metas[index] = meta;
	// Release: readers that acquire the new count see the entry.
	ComponentType_Private::count.store(index + 1, std::memory_order_release);
	return static_cast<ComponentId>(index);
}

const ComponentMeta& ComponentTypeRegistry::Get(ComponentId id) noexcept
{
	MYRIAS_ASSERT(id < ComponentType_Private::count.load(std::memory_order_acquire), "unknown ComponentId");
	return ComponentType_Private::metas[id];
}

std::size_t ComponentTypeRegistry::Count() noexcept
{
	return ComponentType_Private::count.load(std::memory_order_acquire);
}

ComponentId ComponentType_Private::Register(const ComponentMeta& meta) noexcept
{
	return ComponentTypeRegistry::Register(meta);
}

const ComponentMeta& GetComponentMeta(ComponentId id) noexcept
{
	return ComponentTypeRegistry::Get(id);
}
