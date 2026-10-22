#include "Core/Fatal.h"

#include <gtest/gtest.h>

#include <stdexcept>

namespace
{
// Not inlinable: the exception must really be thrown, so that terminate runs with it active.
#if defined(_MSC_VER) && !defined(__clang__)
__declspec(noinline)
#else
[[gnu::noinline]]
#endif
#if defined(_MSC_VER) && !defined(__clang__)
#	pragma warning(push)
#	pragma warning(disable : 4297) // intended: the test makes the exception escape a noexcept function
#endif
void ThrowFromNoexcept() noexcept
{
	throw std::runtime_error("kaboom");
}
#if defined(_MSC_VER) && !defined(__clang__)
#	pragma warning(pop)
#endif
} // namespace

// One death test only: they spawn a process and are slow on Windows.
TEST(FatalDeathTest, FatalErrorAbortsWithMessage)
{
	EXPECT_DEATH(FatalError("boom"), "Fatal error: boom");
}

TEST(FatalDeathTest, TerminateHandlerReportsTheActiveException)
{
#if defined(__clang__)
	// clang-cl: an exception escaping a noexcept function reaches terminate from a pad with no
	// active exception, so the handler can only print the generic message.
	const char* const expected = "no active exception";
#else
	const char* const expected = "kaboom";
#endif
	EXPECT_DEATH(
	    {
		    InstallFatalHandlers();
		    ThrowFromNoexcept();
	    },
	    expected);
}
