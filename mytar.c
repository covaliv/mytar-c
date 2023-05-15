#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <err.h>
#include <stdarg.h>

#define BLOCK_SIZE 512
#define VERBOSE "-v"
#define LIST "-t"
#define EXTRACT "-x"
#define FILENAME "-f"
#define REGTYPE '0'
#define AREGTYPE '\0'

FILE *archive = NULL;

typedef enum ErrorType
{
	ERR_NONE,
	ERR_USAGE,
	ERR_UNKNOWN_OPTION,
	ERR_OPTION_F_REQUIRES_ARG,
	ERR_UNSUPPORTED_HEADER_TYPE,
	ERR_CANNOT_CREATE_FILE,
	ERR_UNEXPECTED_EOF,
	ERR_NOT_RECOVERABLE,
	ERR_CANNOT_OPEN_ARCHIVE,
	ERR_FILE_NOT_FOUND,
	ERR_FAILURE_STATUS,
	ERR_NOT_TAR_ARCHIVE
}

ErrorType;

typedef enum WarningType
{
	WARN_NONE,
	WARN_LONE_ZERO_BLOCK,
	WARN_NOT_FOUND_IN_ARCHIVE
}

WarningType;

typedef struct posix_header
{
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
}

tar_header;

typedef enum operation
{
	OP_NONE,
	OP_LIST,
	OP_EXTRACT
}

operation;

bool is_tar_archive(FILE *archive)
{
	tar_header header;
	char block[BLOCK_SIZE];

	if (fread(block, BLOCK_SIZE, 1, archive) != 1)
	{
		return false;
	}

	memcpy(&header, block, BLOCK_SIZE);
	return strncmp(header.magic, "ustar", 5) == 0;
}

bool is_zero_block(const char *block)
{
	for (int i = 0; i < BLOCK_SIZE; ++i)
	{
		if (*(block + i) != 0)
		{
			return (false);
		}
	}

	return (true);
}

void immediate_print(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stdout, format, args);
	fflush(stdout);
	va_end(args);
}

void cleanup()
{
	if (archive != NULL)
	{
		fclose(archive);
	}
}

void handle_error(ErrorType error_type, ...)
{
	va_list args;
	va_start(args, error_type);

	switch (error_type)
	{
		case ERR_UNKNOWN_OPTION:
			{
				char *option = va_arg(args, char *);
				warnx("Unknown option: %s", option);
			}

			break;
		case ERR_OPTION_F_REQUIRES_ARG:
			warnx("Option -f requires an argument");
			break;
		case ERR_UNSUPPORTED_HEADER_TYPE:
			{
				int header_type = va_arg(args, int);
				warnx("Unsupported header type: %d", header_type);
			}

			break;
		case ERR_CANNOT_CREATE_FILE:
			{
				char *filename = va_arg(args, char *);
				warnx("Cannot create file: %s", filename);
			}

			break;
		case ERR_UNEXPECTED_EOF:
			warnx("Unexpected EOF in archive");
			handle_error(ERR_NOT_RECOVERABLE);
			break;
		case ERR_NOT_RECOVERABLE:
			warnx("Error is not recoverable: exiting now");
			break;
		case ERR_CANNOT_OPEN_ARCHIVE:
			{
				char *archive_filename = va_arg(args, char *);
				warnx("Cannot open archive: %s", archive_filename);
			}

			break;
		case ERR_FILE_NOT_FOUND:
			{
				char *filename = va_arg(args, char *);
				warnx("File %s not found in archive", filename);
			}

			break;
		case ERR_FAILURE_STATUS:
			warnx("Exiting with failure status due to previous errors");
			break;
		case ERR_USAGE:
			{
				char *usage = va_arg(args, char *);
				warnx("Usage: %s -f[archive-filename] OPERATION (-x|-t) OPTION[-v][file1][file2] ...", usage);
			}

			break;
		case ERR_NOT_TAR_ARCHIVE:
			{
				warnx("This does not look like a tar archive");
				handle_error(ERR_FAILURE_STATUS);
				break;
			}

		default:
			break;
	}

	va_end(args);
	exit(2);
}

void handle_warning(WarningType warning_type, ...)
{
	va_list args;
	va_start(args, warning_type);

	switch (warning_type)
	{
		case WARN_LONE_ZERO_BLOCK:
			{
				int block_number = va_arg(args, int);
				warnx("A lone zero block at %d", block_number);
				break;
			}

		case WARN_NOT_FOUND_IN_ARCHIVE:
			{
				char *filename = va_arg(args, char *);
				warnx("%s: Not found in archive", filename);
				break;
			}

		default:
			break;
	}

	va_end(args);
}

bool string_compare(const char *str1, const char *str2)
{
	return strcmp(str1, str2) == 0;
}

bool is_end_of_archive(const char *block, int *zero_block_count)
{
	if (is_zero_block(block))
	{
		++(*zero_block_count);
		if (*zero_block_count == 2)
		{
			return true;
		}
	}
	else
	{
		*zero_block_count = 0;
	}

	return false;
}

void process_file_not_found(int argc, int filenamesStart, char **foundFiles, char **argv)
{
	bool file_not_found = false;
	for (int i = filenamesStart; i < argc; ++i)
	{
		if (*(foundFiles + i) == NULL)
		{
			handle_warning(WARN_NOT_FOUND_IN_ARCHIVE, *(argv + i));
			file_not_found = true;
		}
	}

	if (file_not_found)
	{
		handle_error(ERR_FAILURE_STATUS);
	}
}

void skip_blocks(FILE *archive, int blocks, char *block)
{
	for (int i = 0; i < blocks; ++i)
	{
		if (fread(block, BLOCK_SIZE, 1, archive) != 1)
		{
			handle_error(ERR_UNEXPECTED_EOF);
		}
	}
}

void parse_args(int argc,
	char **argv,
	bool *verbose,
	operation *op,
	char **archiveFilename,
	int *filenamesStart)
{
	int i = 1;
	while (i < argc)
	{
		if (string_compare(*(argv + i), LIST))
		{ *op = OP_LIST;
			++i;
		}
		else if (string_compare(*(argv + i), EXTRACT))
		{ *op = OP_EXTRACT;
			++i;
		}
		else if (string_compare(*(argv + i), VERBOSE))
		{ *verbose = true;
			++i;
		}
		else if (string_compare(*(argv + i), FILENAME))
		{
			if (i + 1 < argc)
			{ 	*archiveFilename = *(argv + i + 1);
				i += 2;
			}
			else
			{
				handle_error(ERR_OPTION_F_REQUIRES_ARG);
			}
		}
		else
		{
			if (i == 1)
			{
				handle_error(ERR_UNKNOWN_OPTION, *(argv + i));
			}
			else
			{ 	*filenamesStart = i;
				break;
			}
		}
	}
}

void list_files(FILE *archive,
	int argc,
	char **argv,
	int filenamesStart,
	char **foundFiles)
{
	tar_header header;
	char block[BLOCK_SIZE];
	int zero_block_count = 0;

	while (fread(block, BLOCK_SIZE, 1, archive) == 1)
	{
		if (is_end_of_archive(block, &zero_block_count))
		{
			break;
		}
		else if (zero_block_count == 1)
		{
			continue;
		}

		memcpy(&header, block, BLOCK_SIZE);

		if (header.typeflag != REGTYPE && header.typeflag != AREGTYPE)
		{
			handle_error(ERR_UNSUPPORTED_HEADER_TYPE, header.typeflag);
		}

		int fileSize = strtol(header.size, NULL, 8);
		int blocks = fileSize / BLOCK_SIZE;

		if (fileSize % BLOCK_SIZE != 0)
		{ ++blocks;
		}

		if (filenamesStart < argc)
		{
			for (int i = filenamesStart; i < argc; ++i)
			{
				if (strcmp(*(argv + i), header.name) == 0)
				{ 		*(foundFiles + i) = header.name;
					immediate_print("%s\n", header.name);
				}
			}
		}
		else
		{
			immediate_print("%s\n", header.name);
		}

		skip_blocks(archive, blocks, block);
	}

	if (zero_block_count == 1)
	{
		handle_warning(WARN_LONE_ZERO_BLOCK, ftell(archive) / BLOCK_SIZE);
	}

	process_file_not_found(argc, filenamesStart, foundFiles, argv);
}

void extract_files(FILE *archive,
	int argc,
	char **argv,
	int filenamesStart,
	char **foundFiles,
	bool verbose)
{
	tar_header header;
	char block[BLOCK_SIZE];
	int zero_block_count = 0;
	bool file_not_found = false;

	while (fread(block, BLOCK_SIZE, 1, archive) == 1)
	{
		if (is_end_of_archive(block, &zero_block_count))
		{
			break;
		}
		else if (zero_block_count == 1)
		{
			continue;
		}

		memcpy(&header, block, BLOCK_SIZE);

		if (header.typeflag != REGTYPE && header.typeflag != AREGTYPE)
		{
			handle_error(ERR_UNSUPPORTED_HEADER_TYPE, header.typeflag);
		}

		int fileSize = strtol(header.size, NULL, 8);
		int blocks = fileSize / BLOCK_SIZE;

		if (fileSize % BLOCK_SIZE != 0)
		{ ++blocks;
		}

		bool extractFile = true;
		if (filenamesStart < argc)
		{
			extractFile = false;
			for (int i = filenamesStart; i < argc; ++i)
			{
				if (strcmp(*(argv + i), header.name) == 0)
				{ 		*(foundFiles + i) = header.name;
					extractFile = true;
					break;
				}
			}
		}

		if (extractFile)
		{
			FILE *outputFile = fopen(header.name, "wb");
			if (outputFile == NULL)
			{
				handle_error(ERR_CANNOT_CREATE_FILE, header.name);
			}

			if (verbose)
			{
				immediate_print("%s\n", header.name);
			}

			for (int i = 0; i < blocks; ++i)
			{
				if (fread(block, BLOCK_SIZE, 1, archive) != 1)
				{
					fclose(outputFile);
					handle_error(ERR_UNEXPECTED_EOF);
				}

				int bytesToWrite = (i == blocks - 1 && fileSize % BLOCK_SIZE != 0) ? fileSize % BLOCK_SIZE : BLOCK_SIZE;
				fwrite(block, bytesToWrite, 1, outputFile);
			}

			fclose(outputFile);
		}
		else
		{
			skip_blocks(archive, blocks, block);
		}
	}

	if (zero_block_count == 1)
	{
		handle_warning(WARN_LONE_ZERO_BLOCK, ftell(archive) / BLOCK_SIZE);
	}

	if (filenamesStart < argc)
	{
		for (int i = filenamesStart; i < argc; ++i)
		{
			if (*(foundFiles + i) == NULL)
			{
				handle_error(ERR_FILE_NOT_FOUND, *(argv + i));
				file_not_found = true;
			}
		}
	}

	if (file_not_found)
	{
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		handle_error(ERR_USAGE, argv[0]);
	}

	operation op = OP_NONE;
	bool verbose = false;
	char *archiveFilename = NULL;
	int filenamesStart = argc;
	char *foundFiles[argc];

	atexit(cleanup);

	memset(foundFiles, 0, argc* sizeof(char*));
	parse_args(argc, argv, &verbose, &op, &archiveFilename, &filenamesStart);

	if (op == OP_NONE || archiveFilename == NULL)
	{
		handle_error(ERR_USAGE, argv[0]);
	}

	FILE *archive = fopen(archiveFilename, "rb");

	if (archive == NULL)
	{
		handle_error(ERR_CANNOT_OPEN_ARCHIVE, archiveFilename);
	}

	if (!is_tar_archive(archive))
	{
		fclose(archive);
		handle_error(ERR_NOT_TAR_ARCHIVE);
	}

	fseek(archive, 0, SEEK_SET);	// Reset the file pointer to the beginning

	if (op == OP_LIST)
	{
		list_files(archive, argc, argv, filenamesStart, foundFiles);
	}
	else if (op == OP_EXTRACT)
	{
		extract_files(archive, argc, argv, filenamesStart, foundFiles, verbose);
	}

	fclose(archive);

	return 0;
}