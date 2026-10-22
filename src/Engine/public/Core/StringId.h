#pragma once

#include "Core/Assert.h"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

class StringId;

// Debug-only name registry: runtime-built ids register their name, so they can be printed and
// hash collisions are caught. In builds without asserts, Register does nothing and Lookup
// returns "<unknown>".
namespace StringIdDebug
{
// Asserts if the id is already registered with a different name (hash collision).
void Register(StringId id, std::string_view name) noexcept;
[[nodiscard]] std::string_view Lookup(StringId id) noexcept;
} // namespace StringIdDebug

namespace StringId_Private
{
inline constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
inline constexpr std::uint64_t kFnvPrime       = 1099511628211ull;

[[nodiscard]] constexpr std::uint64_t Fnv1a64(std::string_view text) noexcept
{
	std::uint64_t hash = kFnvOffsetBasis;
	for (const char character : text)
	{
		hash ^= static_cast<std::uint8_t>(character);
		hash *= kFnvPrime;
	}
	return hash;
}
} // namespace StringId_Private

// Identifier made from a name: a 64-bit FNV-1a hash, immutable, compared as an integer.
// Use it for names and ids (assets, tags), not for display text.
//
// Build ids at load time or as constants (constexpr ids are hashed at compile time); do not
// hash strings inside the frame. The default value is null.
class StringId
{
public:
	constexpr StringId() noexcept = default;

	constexpr explicit StringId(std::string_view name) noexcept // Debug: registers the name (may allocate)
	    : value(StringId_Private::Fnv1a64(name))
	{
		MYRIAS_ASSERT(value != 0, "StringId: name hashes to the reserved null value");
		if !consteval
		{
			StringIdDebug::Register(*this, name);
		}
	}

	[[nodiscard]] constexpr std::uint64_t Value() const noexcept { return value; }
	[[nodiscard]] constexpr bool          IsNull() const noexcept { return value == 0; }

	friend constexpr bool operator==(StringId, StringId) noexcept                  = default;
	friend constexpr std::strong_ordering operator<=>(StringId, StringId) noexcept = default;

private:
	std::uint64_t value = 0;
};

template <>
struct std::hash<StringId>
{
	[[nodiscard]] std::size_t operator()(StringId id) const noexcept { return static_cast<std::size_t>(id.Value()); }
};
