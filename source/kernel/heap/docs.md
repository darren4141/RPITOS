# heap

Bump allocator over a single static 256 KB array — `heap_malloc()` only, no
free. This is intentional: the kernel doesn't do dynamic allocation in the
usual sense, it's a one-way pool used for task stacks and a handful of
fixed-size buffers set up at startup, never released for the life of the
program.
