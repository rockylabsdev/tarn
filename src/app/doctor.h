#pragma once
// `tarn doctor`: checks the setup in one command, in the client and with no daemon
// needed. One line per check (✓ ok, ! optional or unknown, ✗ required and missing).
// The exit code is 1 only when a required check fails.
namespace tarn {

int runDoctor(int argc, char **argv);

} // namespace tarn
