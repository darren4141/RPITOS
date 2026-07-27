# status

`StatusCode`, the return type used across nearly every function in the
codebase, plus `STATUS_OK_OR_WARN()` for call sites that want to log a
non-OK result without handling it. Header-only; no `.c` file.
