#pragma once

// Per-user "Launch at startup" toggle backed by
// HKCU\Software\Microsoft\Windows\CurrentVersion\Run.
//
// All three functions consult / mutate the registry directly; the registry
// itself is the single source of truth. No process-wide state is cached.

// True if the Run value exists and points at the current executable.
// A value that exists but references a different path is treated as disabled
// so that EnableAutostart() will overwrite it with the current path.
bool IsAutostartEnabled();

// Writes the Run value with the quoted current exe path. Returns true on
// success.
bool EnableAutostart();

// Deletes the Run value. Returns true if the value was removed or was already
// absent.
bool DisableAutostart();
