Tar: A GNU tar compatible tape archiver
=======================================

This is a simple implementation of a very small subset of GNU tar functionality. The primary objective is to be fully compatible with GNU tar.

<!-- Features -->
## Features

- [x] Implements the -t -f -x -v options
- [x] Lists GNU tar archives created via the following invocation: tar -f archive.tar -c <files>
- [x] Extracts GNU tar archives created via the following invocation: tar -f archive.tar -x <files>
- [x] Supports file arguments and only lists/extracts the files specified
- [x] Handles errors and edge cases, such as unsupported header types, truncated archives, and missing zero blocks
- [x] Recognizes and reports if a file is not a valid GNU tar archive

## Compilation

```bash
$ gcc mytar.c -o mytar
```

## Usage

- [x] Listing the contents of an archive:

```bash
$ ./mytar -f archive.tar -t [file1] [file2] ...
```

- [x] Extracting the contents of an archive:

```bash
$ ./mytar -f archive.tar -x [file1] [file2] ...
```

## Limitations

- Assumes files under 2GB
- Assumes only archives exclusively containing regular files
- Does not support collation of options (e.g., -tf archive.tar)