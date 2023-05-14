#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <err.h>
#include <stdarg.h>

#define	BLOCK_SIZE	512

#define	REGTYPE '0'
#define	AREGTYPE '\0'
#define VERBOSE "-v"
#define LIST "-t"
#define EXTRACT "-x"
#define FILENAME "-f"

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

typedef enum operation {
	OP_NONE,
	OP_LIST,
	OP_EXTRACT
} operation;

bool isTarArchive(FILE *archive) {
    tar_header header;
    char block[BLOCK_SIZE];

    if (fread(block, BLOCK_SIZE, 1, archive) != 1) {
        return false;
    }
    memcpy(&header, block, BLOCK_SIZE);
    return strncmp(header.magic, "ustar", 5) == 0;
}

bool isZeroBlock(const char *block) {
	for (int i = 0; i < BLOCK_SIZE; ++i) {
		if (*(block + i) != 0) {
			return (false);
		}
	}
	return (true);
}

void exitError(const char *format, ...) {
	va_list args;
	va_start(args, format);
	vwarnx(format, args);
	va_end(args);
	exit(2);
}


void parseArgs(int argc,
				char **argv,
				bool *verbose,
				operation *op,
				char **archiveFilename,
				int *filenamesStart) {

	int i = 1;
	while (i < argc) {
		if (strcmp(*(argv + i), LIST) == 0) {
			*op = OP_LIST;
			++i;
		} else if (strcmp(*(argv + i), EXTRACT) == 0) {
			*op = OP_EXTRACT;
			++i;
        }
        else if (strcmp(*(argv + i), VERBOSE) == 0) {
            *verbose = true;
            ++i;
		} else if (strcmp(*(argv + i), FILENAME) == 0) {
			if (i + 1 < argc) {
				*archiveFilename = *(argv + i + 1);
                i += 2;
            } else {
                exitError("Option -f requires an argument");
            }
		} else {
			if (i == 1) {
                exitError("Unknown option: %s", *(argv + i));
            }
            else {
                *filenamesStart = i;
                break;
            }
		}
	}
}

void listFiles(FILE *archive,
				int argc,
				char **argv,
				int filenamesStart,
				char **foundFiles) {
	tar_header header;
	char block[BLOCK_SIZE];
	int contZeroBlocks = 0;
	bool fileNFound = false;

	while (fread(block, BLOCK_SIZE, 1, archive) == 1) {
		if (isZeroBlock(block)) {
			++contZeroBlocks;
			if (contZeroBlocks == 2) {
				break;
			}
			continue;
		}

		contZeroBlocks = 0;
		memcpy(&header, block, BLOCK_SIZE);

		if (header.typeflag != REGTYPE && header.typeflag != AREGTYPE) {
			exitError("Unsupported header type: %d", header.typeflag);
		}

		if (filenamesStart < argc) {
			for (int i = filenamesStart; i < argc; ++i) {
				if (strcmp(*(argv + i), header.name) == 0) {
					*(foundFiles + i) = header.name;
					fprintf(stdout, "%s\n", header.name);
					fflush(stdout);
				}
			}
		} else {
			fprintf(stdout, "%s\n", header.name);
			fflush(stdout);
		}

		int fileSize = strtol(header.size, NULL, 8);
		int blocks = fileSize / BLOCK_SIZE;

		if (fileSize % BLOCK_SIZE != 0) {
			++blocks;
		}

		for (int i = 0; i < blocks; ++i) {
			if (fread(block, BLOCK_SIZE, 1, archive) != 1) {
				exitError("Unexpected EOF in archive\nmytar: Error is not recoverable: exiting now");
			}
		}
	}

	if (contZeroBlocks == 1) {
		warnx("A lone zero block at %ld", ftell(archive) / BLOCK_SIZE);
	}

	if (filenamesStart < argc) {
		for (int i = filenamesStart; i < argc; ++i) {
			if (*(foundFiles + i) == NULL) {
				warnx("%s: Not found in archive", *(argv + i));
				fileNFound = true;
			}
		}
	}

	if (fileNFound) {
		exitError("Exiting with failure status due to previous errors");
	}
}

void extractFiles(FILE *archive, 
					int argc,
					char **argv,
					int filenamesStart,
					char **foundFiles,
					bool verbose)
{
	tar_header header;
	char block[BLOCK_SIZE];
	int contZeroBlocks = 0;
    bool fileNFound = false;

	while (fread(block, BLOCK_SIZE, 1, archive) == 1) {
		if (isZeroBlock(block)) {
			++contZeroBlocks;
			if (contZeroBlocks == 2) {
				break;
			}
			continue;
		}

		contZeroBlocks = 0;
		memcpy(&header, block, BLOCK_SIZE);

		if (header.typeflag != REGTYPE && header.typeflag != AREGTYPE) {
			exitError("Unsupported header type: %d", header.typeflag);
		}

		int fileSize = strtol(header.size, NULL, 8);
		int blocks = fileSize / BLOCK_SIZE;

		if (fileSize % BLOCK_SIZE != 0) {
			++blocks;
		}

		bool extractFile = true;
		if (filenamesStart < argc) {
			extractFile = false;
			for (int i = filenamesStart; i < argc; ++i) {
				if (strcmp(*(argv + i), header.name) == 0) {
					*(foundFiles + i) = header.name;
					extractFile = true;
					break;
				}
			}
		}

		if (extractFile) {
			FILE *outputFile = fopen(header.name, "wb");
			if (outputFile == NULL) {
				exitError("Cannot create file %s", header.name);
			}

			if (verbose) {
				fprintf(stdout, "%s\n", header.name);
				fflush(stdout);
			}

			for (int i = 0; i < blocks; ++i) {
				if (fread(block, BLOCK_SIZE, 1, archive) != 1) {
					fclose(outputFile);
					exitError("Unexpected EOF in archive\nmytar: Error is not recoverable: exiting now");
				}

				int bytesToWrite = (i == blocks - 1 && fileSize % BLOCK_SIZE != 0) ? fileSize % BLOCK_SIZE : BLOCK_SIZE;
				fwrite(block, bytesToWrite, 1, outputFile);
			}

			fclose(outputFile);
		} else {
			for (int i = 0; i < blocks; ++i) {
				if (fread(block, BLOCK_SIZE, 1, archive) != 1) {
					exitError("Unexpected EOF in archive\nmytar: Error is not recoverable: exiting now");
				}
			}
		}
	}

    if (contZeroBlocks == 1) {
		warnx("A lone zero block at %ld", ftell(archive) / BLOCK_SIZE);
	}

    if (filenamesStart < argc) {
        for (int i = filenamesStart; i < argc; ++i) {
            if (*(foundFiles + i) == NULL) {
                warnx("%s: Not found in archive", *(argv + i));
                fileNFound = true;
            }
        }
    }

    if (fileNFound) {
        exitError("Exiting with failure status due to previous errors");
    }
}

int main(int argc, char **argv) {
	if (argc < 3) {
		exitError("Usage: %s -f -t [archive-filename] [file1] [file2] ...", argv[0]);
	}

	operation op = OP_NONE;
	bool verbose = false;
	char *archiveFilename = NULL;
	int filenamesStart = argc;
	char *foundFiles[argc];

	memset(foundFiles, 0, sizeof (foundFiles));
	parseArgs(argc, argv, &verbose, &op, &archiveFilename, &filenamesStart);
	if (op == OP_NONE || archiveFilename == NULL) {
		exitError("Usage: %s -f -t [archive-filename] [file1] [file2] ...", argv[0]);
	}

	FILE *archive = fopen(archiveFilename, "rb");

    if (archive == NULL) {
        exitError("Cannot open archive file %s", archiveFilename);
    }

    if (!isTarArchive(archive)) {
        fclose(archive);
        exitError("This does not look like a tar archive\nmytar: Exiting with failure status due to previous errors");
    }

    fseek(archive, 0, SEEK_SET); // Reset the file pointer to the beginning

    if (op == OP_LIST) {
        listFiles(archive, argc, argv, filenamesStart, foundFiles);
    } else if (op == OP_EXTRACT) {
        extractFiles(archive, argc, argv, filenamesStart, foundFiles, verbose);
    }
    fclose(archive);
}