#include "Core/StringId.h"

#if MYRIAS_ASSERTS_ENABLED
#	include <cstdio>
#	include <mutex>
#	include <string>
#	include <unordered_map>
#endif

#if MYRIAS_ASSERTS_ENABLED

namespace StringIdDebug_Private
{
namespace
{
// Process-wide debug diagnostics, shared by every World and thread (append-only, mutex-guarded).
struct Registry
{
	std::mutex                                   mutex;
	std::unordered_map<std::uint64_t, std::string> names; // node-based: stored strings never move
};

Registry& GetRegistry()
{
	static Registry registry;
	return registry;
}
} // namespace
} // namespace StringIdDebug_Private

void StringIdDebug::Register(StringId id, std::string_view name) noexcept
{
	auto&                  registry = StringIdDebug_Private::GetRegistry();
	const std::scoped_lock lock(registry.mutex);

	const auto [it, bInserted] = registry.names.try_emplace(id.Value(), name);
	if (!bInserted && it->second != name)
	{
		std::fprintf(stderr, "StringId collision: \"%s\" and \"%.*s\" share hash 0x%016llx\n", it->second.c_str(),
		             static_cast<int>(name.size()), name.data(), static_cast<unsigned long long>(id.Value()));
		MYRIAS_ASSERT(false, "StringId collision");
	}
}

std::string_view StringIdDebug::Lookup(StringId id) noexcept
{
	auto&                  registry = StringIdDebug_Private::GetRegistry();
	const std::scoped_lock lock(registry.mutex);

	const auto it = registry.names.find(id.Value());
	return it != registry.names.end() ? std::string_view(it->second) : std::string_view("<unknown>");
}

#else

void StringIdDebug::Register(StringId, std::string_view) noexcept
{
}

std::string_view StringIdDebug::Lookup(StringId) noexcept
{
	return "<unknown>";
}

#endif
