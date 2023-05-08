#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <err.h>
#include <stdarg.h>

#define BLOCK_SIZE 512

typedef struct posix_header { 
    char name[100];               
    char mode[8];                 
    char uid[8];                  
    char gid[8];                  
    char size[12];                
    char mtime[12];               
    char chksum[8];               
    char typeflag;                
    char linkname[100];           
    char magic[6];                
    char version[2];              
    char uname[32];               
    char gname[32];               
    char devmajor[8];             
    char devminor[8];             
    char prefix[155];             
    char pad[12];                 
} tar_header;

#define REGTYPE  '0'
#define AREGTYPE '\0'
#define LNKTYPE  '1'
#define SYMTYPE  '2'
#define CHRTYPE  '3'
#define BLKTYPE  '4'
#define DIRTYPE  '5'
#define FIFOTYPE '6'
#define CONTTYPE '7'

bool is_zero_block(const char *block) {
    for (int i = 0; i < BLOCK_SIZE; ++i) {
        if (*(block + i) != 0) {
            return false;
        }
    }
    return true;
}

void exit_with_error(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vwarnx(format, args);
    va_end(args);
    exit(2);
}

void parse_args(int argc, char **argv, bool *files_listed, char **archive_filename, int *filenames_start) {
    int i = 1;
    while (i < argc) {
        if (strcmp(*(argv + i), "-t") == 0) {
            *files_listed = true;
        } else if (strcmp(*(argv + i), "-f") == 0) {
            if (i + 1 < argc) {
                *archive_filename = *(argv + i + 1);
                ++i;
            } else {
                exit_with_error("Option -f requires an argument");
            }
        } else {
            *filenames_start = i;
            break;
        }
        ++i;
    }
}

void list_files(FILE *archive, int argc, char **argv, int filenames_start, char **found_files) {
    tar_header header;
    char block[BLOCK_SIZE];
    int continuous_zero_blocks = 0;
    bool not_found = false;

    while (fread(block, BLOCK_SIZE, 1, archive) == 1) {
        if (is_zero_block(block)) {
            ++continuous_zero_blocks;
            if (continuous_zero_blocks == 2) {
                break;
            }
            continue;
        }

        continuous_zero_blocks = 0;
        memcpy(&header, block, BLOCK_SIZE);

        if (header.typeflag != REGTYPE && header.typeflag != AREGTYPE) {
            exit_with_error("Unsupported header type: %d", header.typeflag);
        }

        if (filenames_start < argc) {
            for (int i = filenames_start; i < argc; ++i) {
                if (strcmp(*(argv + i), header.name) == 0) {
                    *(found_files + i) = header.name;
                    fprintf(stdout, "%s\n", header.name);
                    fflush(stdout);
                }
            }
        } else {
            fprintf(stdout, "%s\n", header.name);
            fflush(stdout);
        }

        int file_size = strtol(header.size, NULL, 8);
        int blocks = file_size / BLOCK_SIZE;

        if (file_size % BLOCK_SIZE != 0) {
            ++blocks;
        }

        for (int i = 0; i < blocks; ++i) {
            if (fread(block, BLOCK_SIZE, 1, archive) != 1) {
                exit_with_error("Unexpected EOF in archive\nmytar: Error is not recoverable: exiting now");
            }
        }
    }

    if (continuous_zero_blocks == 1) {
        // exit_with_error("A lone zero block at %ld", ftell(archive) / BLOCK_SIZE);
        errx(0, "A lone zero block at %ld", ftell(archive) / BLOCK_SIZE);
    }

    if (filenames_start < argc) {
        for (int i = filenames_start; i < argc; ++i) {
            if (*(found_files + i) == NULL) {
                warnx("%s: Not found in archive", *(argv + i));
                not_found = true;
            }
        }
    }

    if (not_found) {
        exit_with_error("Exiting with failure status due to previous errors");
    }
}

int main(int argc, char **argv) {
    if (argc < 3) {
        exit_with_error("Usage: %s -f -t [archive-filename] [file1] [file2] ...", argv[0]);
    }

    bool files_listed = false;
    char *archive_filename = NULL;
    int filenames_start = argc;
    char *found_files[argc];
    memset(found_files, 0, sizeof(found_files));
    parse_args(argc, argv, &files_listed, &archive_filename, &filenames_start);

    if (!files_listed || archive_filename == NULL) {
        exit_with_error("Usage: %s -f -t [archive-filename] [file1] [file2] ...", argv[0]);
    }

    FILE *archive = fopen(archive_filename, "rb");

    if (archive == NULL) {
        exit_with_error("Cannot open archive file %s", archive_filename);
    }

    list_files(archive, argc, argv, filenames_start, found_files);
    
    fclose(archive);
}