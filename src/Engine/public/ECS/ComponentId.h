#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>

// Index of a component type in per-type arrays. Process-wide: the same in every World.
// Never serialize it (it depends on registration order); use ComponentMeta::name instead.
using ComponentId = std::uint16_t;

inline constexpr std::size_t kMaxComponentTypes = 512;

// One bit per component type: archetype signatures, query filters, system access sets.
using ComponentMask = std::bitset<kMaxComponentTypes>;
