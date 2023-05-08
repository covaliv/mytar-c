Tar: A GNU tar compatible tape archiver
=======================================

This is a simple implementation of a very small subset of GNU tar functionality. The primary objective is to be fully compatible with GNU tar.

<!-- Features -->
## Features

- [x] Implements the -t and -f options
- [x] Lists GNU tar archives created via the following invocation: tar -f archive.tar -c <files>
- [x] Supports file arguments and only lists those in the archive
- [x] Handles errors and edge cases, such as unsupported header types, truncated archives, and missing zero blocks

## Usage

```bash
$ ./mytar -f archive.tar -t [file1] [file2] ...
```

## Compilation

```bash
$ gcc -Wall -Wextra -std=c99 -o mytar mytar.c
```

## Limitations

- Assumes files under 2GB
- Assumes only archives exclusively containing regular files
- Does not support collation of options (e.g., -tf archive.tar)