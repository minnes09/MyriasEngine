#pragma once

// Debug assertions. On by default in Debug, off in Release; force either way by defining
// MYRIAS_ASSERTS_ENABLED (0 or 1) before including this header (e.g. a future profiling build).
//
// Every translation unit that includes engine headers must use the same MYRIAS_ASSERTS_ENABLED
// value (like NDEBUG for assert): some engine classes compile debug-only code into inline
// functions, and mixing settings is an ODR violation.
//
// A failing assertion prints the expression, the message and the location, then aborts.
// When disabled the expression is not evaluated, but it is still compiled: it must stay valid
// and it produces no "unused variable" warnings.

#ifndef MYRIAS_ASSERTS_ENABLED
#	ifdef NDEBUG
#		define MYRIAS_ASSERTS_ENABLED 0
#	else
#		define MYRIAS_ASSERTS_ENABLED 1
#	endif
#endif

namespace Assert_Private
{
[[noreturn]] void Fail(const char* expression, const char* message, const char* file, int line) noexcept;
} // namespace Assert_Private

#if MYRIAS_ASSERTS_ENABLED
#	define MYRIAS_ASSERT(expression, message) \
		((expression) ? static_cast<void>(0) : Assert_Private::Fail(#expression, message, __FILE__, __LINE__))
#else
#	define MYRIAS_ASSERT(expression, message) static_cast<void>(sizeof(!(expression)))
#endif
