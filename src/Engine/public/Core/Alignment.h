#pragma once

#include <cstddef>

[[nodiscard]] constexpr std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept
{
	return (value + alignment - 1) / alignment * alignment;
}
