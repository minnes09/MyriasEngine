#pragma once

#include <source_location>
#include <string_view>

namespace TypeName_Private
{
template <typename T>
[[nodiscard]] constexpr std::string_view RawTypeName() noexcept
{
	return std::source_location::current().function_name();
}
} // namespace TypeName_Private

// Name of T extracted from the compiler's function signature, e.g.
//   MSVC:  "... RawTypeName<struct Foo>(void)"   Clang: "... RawTypeName() [T = Foo]"
// Compiler-specific spelling: for diagnostics only, never serialize it.
template <typename T>
[[nodiscard]] constexpr std::string_view TypeName() noexcept
{
	std::string_view name = TypeName_Private::RawTypeName<T>();
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
