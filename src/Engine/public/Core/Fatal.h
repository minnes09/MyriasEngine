#pragma once

// Fatal errors (ADR-0005): out of memory, capacity limits, anything the process cannot recover from.

// Prints "Fatal error: <message>" on stderr and aborts. The single exit point for fatal errors.
[[noreturn]] void FatalError(const char* message) noexcept;

// Makes termination explain itself: installs a terminate handler that prints the active exception's
// message, if any (a std::bad_alloc escaping a noexcept function ends up here; new(std::nothrow)
// keeps returning nullptr, there is no new-handler). Called once by the application at startup, never
// from a static initialiser of the library; tests do not call it.
void InstallFatalHandlers() noexcept;
