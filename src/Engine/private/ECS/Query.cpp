#include "ECS/QueryId.h"

#include "Core/Fatal.h"

#include <atomic>

namespace Query_Private
{
QueryId AllocateQueryId() noexcept
{
	// Rare (once per query type): no table, the id only indexes each World's fixed queryLookup array.
	static std::atomic<QueryId> next{ 0 };
	const QueryId id = next.fetch_add(1, std::memory_order_relaxed);
	if (id >= kMaxQueryTypes) // always checked: the id indexes a fixed-size table in every World
	{
		FatalError("Query: too many query types: raise kMaxQueryTypes");
	}
	return id;
}
} // namespace Query_Private
