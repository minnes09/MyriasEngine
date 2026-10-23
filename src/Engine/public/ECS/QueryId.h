#pragma once

#include <cstddef>
#include <cstdint>

// Process-wide id of a query type (a list of terms), used to index each World's QueryCache table.
using QueryId = std::uint32_t;

// Capacity of every World's query table (one slot per query type used in the process; the table
// costs sizeof(void*) per slot per World). Exceeding it aborts. Change it here only.
inline constexpr std::size_t kMaxQueryTypes = 1024;

namespace Query_Private
{
[[nodiscard]] QueryId AllocateQueryId() noexcept;
} // namespace Query_Private

template <typename... Terms>
[[nodiscard]] QueryId GetQueryId() noexcept
{
	static const QueryId id = Query_Private::AllocateQueryId();
	return id;
}
