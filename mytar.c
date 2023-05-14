#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <err.h>
#include <stdarg.h>

#define    BLOCK_SIZE    512

#define VERBOSE "-v"
#define LIST "-t"
#define EXTRACT "-x"
#define FILENAME "-f"

#define F_MISSING_ARG "Option -f requires an argument"
#define NOT_TAR_FILE "This does not look like a tar archive"

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

#define    REGTYPE '0'
#define    AREGTYPE '\0'

typedef enum operation {
	OP_NONE,
	OP_LIST,
	OP_EXTRACT
} operation;

bool is_tar_archive(FILE *archive) {
	tar_header header;
	char block[BLOCK_SIZE];

	if (fread(block, BLOCK_SIZE, 1, archive) != 1) {
		return false;
	}
	memcpy(&header, block, BLOCK_SIZE);
	return strncmp(header.magic, "ustar", 5) == 0;
}

bool is_zero_block(const char *block) {
	for (int i = 0; i < BLOCK_SIZE; ++i) {
		if (*(block + i) != 0) {
			return (false);
		}
	}
	return (true);
}

void print_error_and_exit(const char *format, ...) {
	va_list args;
	va_start(args, format);
	vwarnx(format, args);
	va_end(args);
	exit(2);
}

void print_warning(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vwarnx(format, args);
    va_end(args);
}

bool string_compare(const char *str1, const char *str2) {
	return strcmp(str1, str2) == 0;
}

bool is_end_of_archive(const char *block, int *zero_block_count) {
	if (is_zero_block(block)) {
		++(*zero_block_count);
		if (*zero_block_count == 2) {
			return true;
		}
	} else {
		*zero_block_count = 0;
	}
	return false;
}

void process_file_not_found(int argc, int filenamesStart, char **foundFiles, char **argv) {
	bool file_not_found = false;
	for (int i = filenamesStart; i < argc; ++i) {
		if (*(foundFiles + i) == NULL) {
			print_warning("%s: Not found in archive", *(argv + i));
			file_not_found = true;
		}
	}
	if (file_not_found) {
		print_error_and_exit("Exiting with failure status due to previous errors");
	}
}

void skip_blocks(FILE *archive, int blocks, char *block) {
	for (int i = 0; i < blocks; ++i) {
		if (fread(block, BLOCK_SIZE, 1, archive) != 1) {
			print_error_and_exit("Unexpected EOF in archive\nmytar: Error is not recoverable: exiting now");
		}
	}
}

void parse_args(int argc,
                char **argv,
                bool *verbose,
                operation *op,
                char **archiveFilename,
                int *filenamesStart) {

	int i = 1;
	while (i < argc) {
		if (string_compare(*(argv + i), LIST)) {
			*op = OP_LIST;
			++i;
		} else if (string_compare(*(argv + i), EXTRACT)) {
			*op = OP_EXTRACT;
			++i;
		} else if (string_compare(*(argv + i), VERBOSE)) {
			*verbose = true;
			++i;
		} else if (string_compare(*(argv + i), FILENAME)) {
			if (i + 1 < argc) {
				*archiveFilename = *(argv + i + 1);
				i += 2;
			} else {
				print_error_and_exit("Option -f requires an argument");
			}
		} else {
			if (i == 1) {
				print_error_and_exit("Unknown option: %s", *(argv + i));
			} else {
				*filenamesStart = i;
				break;
			}
		}
	}
}


void list_files(FILE *archive,
                int argc,
                char **argv,
                int filenamesStart,
                char **foundFiles) {
	tar_header header;
	char block[BLOCK_SIZE];
	int zero_block_count = 0;

	while (fread(block, BLOCK_SIZE, 1, archive) == 1) {
		// if (is_zero_block(block)) {
		// 	++zero_block_count;
		// 	if (zero_block_count == 2) {
		// 		break;
		// 	}
		// 	continue;
		// }

		if (is_end_of_archive(block, &zero_block_count)) {
			break;
		} else if (zero_block_count == 1) {
			continue;
		}

		memcpy(&header, block, BLOCK_SIZE);

		if (header.typeflag != REGTYPE && header.typeflag != AREGTYPE) {
			print_error_and_exit("Unsupported header type: %d", header.typeflag);
		}

		int fileSize = strtol(header.size, NULL, 8);
		int blocks = fileSize / BLOCK_SIZE;

		if (fileSize % BLOCK_SIZE != 0) {
			++blocks;
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


		skip_blocks(archive, blocks, block);
	}

	if (zero_block_count == 1) {
		print_warning("A lone zero block at %ld", ftell(archive) / BLOCK_SIZE);
	}

	process_file_not_found(argc, filenamesStart, foundFiles, argv);
}

void extract_files(FILE *archive,
                   int argc,
                   char **argv,
                   int filenamesStart,
                   char **foundFiles,
                   bool verbose) {
	tar_header header;
	char block[BLOCK_SIZE];
	int zero_block_count = 0;
	bool file_not_found = false;

	while (fread(block, BLOCK_SIZE, 1, archive) == 1) {
		if (is_end_of_archive(block, &zero_block_count)) {
			break;
		} else if (zero_block_count == 1) {
			continue;
		}

		memcpy(&header, block, BLOCK_SIZE);

		if (header.typeflag != REGTYPE && header.typeflag != AREGTYPE) {
			print_error_and_exit("Unsupported header type: %d", header.typeflag);
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
				print_error_and_exit("Cannot create file %s", header.name);
			}

			if (verbose) {
				fprintf(stdout, "%s\n", header.name);
				fflush(stdout);
			}

			for (int i = 0; i < blocks; ++i) {
				if (fread(block, BLOCK_SIZE, 1, archive) != 1) {
					fclose(outputFile);
					print_warning("Unexpected EOF in archive");
					print_error_and_exit("Error is not recoverable: exiting now");
				}

				int bytesToWrite = (i == blocks - 1 && fileSize % BLOCK_SIZE != 0) ? fileSize % BLOCK_SIZE : BLOCK_SIZE;
				fwrite(block, bytesToWrite, 1, outputFile);
			}

			fclose(outputFile);
		} else {
			skip_blocks(archive, blocks, block);
		}
	}

	if (zero_block_count == 1) {
		print_warning("A lone zero block at %ld", ftell(archive) / BLOCK_SIZE);
	}

	if (filenamesStart < argc) {
		for (int i = filenamesStart; i < argc; ++i) {
			if (*(foundFiles + i) == NULL) {
				print_warning("File %s not found in archive", *(argv + i));
				file_not_found = true;
			}
		}
	}

	if (file_not_found) {
		print_error_and_exit("Exiting with failure status due to previous errors");
	}
}

int main(int argc, char **argv) {
	if (argc < 3) {
		print_error_and_exit("Usage: %s -f -t [archive-filename] [file1] [file2] ...", argv[0]);
	}

	operation op = OP_NONE;
	bool verbose = false;
	char *archiveFilename = NULL;
	int filenamesStart = argc;
	char *foundFiles[argc];

	// set all foundFiles to NULL
	memset(foundFiles, 0, argc * sizeof(char *));
	parse_args(argc, argv, &verbose, &op, &archiveFilename, &filenamesStart);
	if (op == OP_NONE || archiveFilename == NULL) {
		print_error_and_exit("Usage: %s -f -t [archive-filename] [file1] [file2] ...", argv[0]);
	}

	FILE *archive = fopen(archiveFilename, "rb");

	if (archive == NULL) {
		print_error_and_exit("Cannot open archive file %s", archiveFilename);
	}

	if (!is_tar_archive(archive)) {
		fclose(archive);
		print_error_and_exit("This does not look like a tar archive\nmytar: Exiting with failure status due to previous errors");
	}

	fseek(archive, 0, SEEK_SET); // Reset the file pointer to the beginning

	if (op == OP_LIST) {
		list_files(archive, argc, argv, filenamesStart, foundFiles);
	} else if (op == OP_EXTRACT) {
		extract_files(archive, argc, argv, filenamesStart, foundFiles, verbose);
	}
	fclose(archive);
}