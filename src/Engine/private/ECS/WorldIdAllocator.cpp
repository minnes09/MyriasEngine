#include "WorldIdAllocator.h"

#include "Core/Assert.h"
#include "Core/Fatal.h"

#include <bitset>
#include <mutex>

namespace WorldIdAllocator_Private
{
namespace
{
constexpr unsigned kWorldIdCount = 256; // 0 is "no world"

// Process-wide, mutex-guarded: Worlds are created and destroyed rarely.
struct State
{
	std::mutex               mutex;
	std::bitset<kWorldIdCount> used{ 1 }; // bit 0 reserved
	unsigned                 cursor = 1;
};

State& GetState()
{
	static State state;
	return state;
}
} // namespace
} // namespace WorldIdAllocator_Private

WorldId WorldIdAllocator::Acquire() noexcept
{
	using namespace WorldIdAllocator_Private;
	State&                 state = GetState();
	const std::scoped_lock lock(state.mutex);

	for (unsigned attempt = 1; attempt < kWorldIdCount; ++attempt)
	{
		const unsigned candidate = state.cursor;
		state.cursor             = candidate % (kWorldIdCount - 1) + 1; // 1..255, wrapping
		if (!state.used.test(candidate))
		{
			state.used.set(candidate);
			return static_cast<WorldId>(candidate);
		}
	}
	FatalError("too many Worlds alive: WorldId supports 255");
}

void WorldIdAllocator::Release(WorldId world) noexcept
{
	using namespace WorldIdAllocator_Private;
	State&                 state = GetState();
	const std::scoped_lock lock(state.mutex);
	MYRIAS_ASSERT(world != 0 && state.used.test(world), "WorldIdAllocator::Release: id not in use");
	state.used.reset(world);
}
