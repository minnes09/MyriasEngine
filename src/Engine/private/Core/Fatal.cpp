#include "Core/Fatal.h"

#include <cstdio>
#include <cstdlib>
#include <exception>

namespace Fatal_Private
{
namespace
{
[[noreturn]] void OnTerminate() noexcept
{
	if (const std::exception_ptr active = std::current_exception())
	{
		// The only try/catch allowed in the engine (ADR-0005): it never recovers, it only reads what()
		// of the exception that is about to kill the process, for the diagnostic message.
		try
		{
			std::rethrow_exception(active);
		}
		catch (const std::exception& exception)
		{
			std::fprintf(stderr, "Fatal error: unhandled exception: %s\n", exception.what());
		}
		catch (...)
		{
			std::fputs("Fatal error: unhandled exception of unknown type\n", stderr);
		}
	}
	else
	{
		std::fputs("Fatal error: std::terminate called with no active exception (clang-cl reports an exception "
		           "escaping a noexcept function, e.g. std::bad_alloc, this way too)\n",
		           stderr);
	}
	std::fflush(stderr);
	std::abort();
}
} // namespace
} // namespace Fatal_Private

void FatalError(const char* message) noexcept
{
	std::fprintf(stderr, "Fatal error: %s\n", message);
	std::fflush(stderr);
	std::abort();
}

void InstallFatalHandlers() noexcept
{
	std::set_terminate(&Fatal_Private::OnTerminate);
}
