/**
 * Melo Melo Search!
 * Search a byte sequence in sequence file by melody.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>

#define APP_NAME	"Melo Melo Search"
#define APP_VER		"[2013-12-05]"
#define APP_AUTHOR	"loveemu <loveemu.googlecode.com>"

#define COUNT(a)	(sizeof(a) / sizeof(a[0]))

#define MELO_MAX_NOTE_DIST_DEFAULT	6

// Command path (set by main)
char *glCommandPath = NULL;
// Input filename
char glInFilename[512] = { '\0' };
// ...
bool glQuiet = false;

// define byte
typedef unsigned char byte;

// define generic note structure
typedef struct
{
	int time;
	int key;
	int duration;
} SeqNote;

#define NOTE_KEY_REST	0x1000
#define NOTE_KEY_TIE	0x1001

// simple mml parser, returns an array of notes
bool parseMML(SeqNote *notes, int &noteCount, int maxNotes, const char *mml)
{
	if (notes == NULL || mml == NULL)
	{
		return false;
	}

	int i = 0;
	int time = 0;
	int octave = 4;
	int timebase = 48;
	int defaultLength = 4;
	char *endp = NULL;
	noteCount = 0;
	while (mml[i] != '\0')
	{
		char c = tolower(mml[i]);

		if (isspace(c))
		{
			// do nothing
		}
		else if (c == 't')
		{
			i++;
			double tempo = strtod(&mml[i], &endp);
			if (endp == &mml[i])
			{
				fprintf(stderr, "Error: Illegal tempo number '%c'\n", mml[i]);
				return false;
			}
			if (tempo <= 0.0)
			{
				fprintf(stderr, "Error: Illegal tempo '%.1f'\n", tempo);
				return false;
			}
			i += (endp - &mml[i]);
		}
		else if (c == 'o')
		{
			i++;
			octave = strtol(&mml[i], &endp, 10);
			if (endp == &mml[i])
			{
				fprintf(stderr, "Error: Illegal octave number '%c'\n", mml[i]);
				return false;
			}
			i += (endp - &mml[i]);
		}
		else if (c == 'l')
		{
			i++;
			defaultLength = strtol(&mml[i], &endp, 10);
			if (endp == &mml[i])
			{
				fprintf(stderr, "Error: Illegal default length '%c'\n", mml[i]);
				return false;
			}
			if (defaultLength <= 0)
			{
				fprintf(stderr, "Error: Illegal default length '%d'\n", defaultLength);
				return false;
			}
			i += (endp - &mml[i]);
		}
		else if (c == '<')
		{
			octave++;
			i++;
		}
		else if (c == '>')
		{
			octave--;
			i++;
		}
		else if ((c >= 'a' && c <= 'g') || c == 'r' || c == '^')
		{
			int key = 0;
			if (c >= 'a' && c <= 'g')
			{
				int keys[] = { 9, 11, 0, 2, 4, 5, 7 };
				key = keys[c - 'a'] + (octave * 12);
			}
			else if (c == 'r')
			{
				key = NOTE_KEY_REST;
			}
			else if (c == '^')
			{
				key = NOTE_KEY_TIE;
			}
			i++;

			while (mml[i] == '+' || mml[i] == '-')
			{
				if (mml[i] == '+')
				{
					key++;
				}
				else if (mml[i] == '-')
				{
					key++;
				}
				i++;
			}

			int length = strtol(&mml[i], &endp, 10);
			if (endp == &mml[i])
			{
				// use default length
				length = defaultLength;
			}
			i += (endp - &mml[i]);

			int baseDuration = (timebase * 4) / length;
			int duration = baseDuration;
			int dotCount = 0;
			while (mml[i] == '.')
			{
				dotCount++;
				duration += (baseDuration >> dotCount);
				i++;
			}

			if (length <= 0)
			{
				fprintf(stderr, "Error: length must be greater than 0\n");
				return false;
			}

			if (c >= 'a' && c <= 'g')
			{
				memset(&notes[noteCount], 0, sizeof(SeqNote));
				notes[noteCount].time = time;
				notes[noteCount].key = key;
				notes[noteCount].duration = duration;
				noteCount++;
			}
			else if (c == '^')
			{
				fprintf(stderr, "Error: Tie is not supported\n");
				return false;

				// the following code will interpret c4^c4 as c4c4
				// so disable it to avoid confusion

				//if (noteCount > 0)
				//{
				//	notes[noteCount - 1].duration += duration;
				//}
			}

			time += duration;
		}
		else
		{
			fprintf(stderr, "Error: Unknown character '%c'\n", c);
			return false;
		}
	}

	return true;
}

/**
 * Show usage of the application.
 */
void printUsage(void)
{
	printf("%s %s - %s\n", APP_NAME, APP_VER, APP_AUTHOR);
	printf("=============================\n");
	printf("\n");
	printf("Small utility to search a byte sequence by melody.\n");
	printf("\n");
	printf("Note: \n");
	printf("The search engine uses only the key of notes.\n");
	printf("Others, such as lengths, will be ignored.\n");
	printf("\n");
	printf("Usage\n");
	printf("-----\n");
	printf("\n");
	printf("### Syntax ###\n");
	printf("\n");
	printf("%s (options) [input file] [MML]\n", glCommandPath);
	printf("\n");
	printf("### Options ###\n");
	printf("\n");
	printf("--help\n");
	printf("  : show this help\n");
	printf("-q\n");
	printf("  : quiet mode, prints only errors and offsets\n");
	printf("-l<length>\n");
	printf("  : max distance between notes (in bytes) (default: -l%d)\n", MELO_MAX_NOTE_DIST_DEFAULT);
}

/**
 * Search melody candidate bytes from file and mml.
 */
bool searchNotes(FILE *inFile, const char *mml, int maxNoteDist)
{
	bool found = false;

	// closable objects
	int *minOffsets = NULL;
	int *maxOffsets = NULL;

	long fileSize;

	if (maxNoteDist < 1)
	{
		fprintf(stderr, "Error: search length too small\n");
		goto finish;
	}
	else if (maxNoteDist > 16)
	{
		fprintf(stderr, "Error: search length too large\n");
		goto finish;
	}

	// parse MML
	SeqNote notes[512];
	int noteCount;
	if (!parseMML(notes, noteCount, COUNT(notes), mml))
	{
		goto finish;
	}

	// make keys relative to the first note
	for (int i = 1; i < noteCount; i++)
	{
		notes[i].key -= notes[0].key;
	}
	notes[0].key = 0;

	// get whole file size
	fseek(inFile, 0, SEEK_END);
	fileSize = ftell(inFile);

	minOffsets = (int*) calloc(noteCount, sizeof(int));
	if (minOffsets == NULL)
	{
		fprintf(stderr, "Error: Memory allocation failed\n");
		goto finish;
	}
	maxOffsets = (int*) calloc(noteCount, sizeof(int));
	if (maxOffsets == NULL)
	{
		fprintf(stderr, "Error: Memory allocation failed\n");
		goto finish;
	}

	// search...
	for (long offset = 0; offset < fileSize; offset++)
	{
		fseek(inFile, offset, SEEK_SET);
		byte firstByte = (byte) fgetc(inFile);

		// byte range check
		for (int noteIndex = 1; noteIndex < noteCount; noteIndex++)
		{
			int byteValue = (int)firstByte + notes[noteIndex].key;
			if (byteValue < 0 || byteValue > 0xff)
			{
				// overflow
				continue;
			}
		}

		minOffsets[0] = 0;
		maxOffsets[0] = 0;
		int noteIndex;
		for (noteIndex = 1; noteIndex < noteCount; noteIndex++)
		{
			int minOffset = minOffsets[noteIndex - 1] + 1;
			int maxOffset = minOffsets[noteIndex - 1] + maxNoteDist;
			byte targetByte = (byte)(firstByte + notes[noteIndex].key);
			bool targetByteFound = false;

			minOffsets[noteIndex] = INT_MAX;
			maxOffsets[noteIndex] = INT_MIN;
			fseek(inFile, offset + minOffset, SEEK_SET);
			for (int off = minOffset; off <= maxOffset; off++)
			{
				int c = fgetc(inFile);
				if (c == EOF)
				{
					break;
				}
				if (c == targetByte)
				{
					targetByteFound = true;
					if (minOffsets[noteIndex] > off)
					{
						minOffsets[noteIndex] = off;
					}
					if (maxOffsets[noteIndex] < off)
					{
						maxOffsets[noteIndex] = off;
					}
				}
			}
			if (!targetByteFound)
			{
				break;
			}
		}
		if (noteIndex == noteCount)
		{
			found = true;

			if (glQuiet)
			{
				printf("%08lX\n", offset);
			}
			else
			{
				printf("- %08lX: %02X", offset, firstByte);
				for (int noteIndex = 1; noteIndex < noteCount; noteIndex++)
				{
					int byteValue = (int)firstByte + notes[noteIndex].key;
					printf(" %02X", byteValue);
				}
				printf("\n");
			}
		}
	}

finish:
	if (found && !glQuiet)
	{
		printf("\n");
		printf("Note that the above dump omits bytes in between note numbers.\n");
	}
	if (minOffsets != NULL)
	{
		free(minOffsets);
	}
	if (maxOffsets != NULL)
	{
		free(maxOffsets);
	}
	return found;
}

/**
 * Program main.
 */
int main(int argc, char *argv[])
{
	int ret = EXIT_FAILURE;

	// user options
	int meloMaxNoteDist = MELO_MAX_NOTE_DIST_DEFAULT;

	// closable objects
	FILE *inFile = NULL;

	char *mml = NULL;

	// set command path
	glCommandPath = argv[0];

	// parse options
	int argi = 1;
	char *endp = NULL;
	while (argi < argc && argv[argi][0] == '-')
	{
		if (strcmp(argv[argi], "--help") == 0)
		{
			printUsage();
			goto finish;
		}
		if (strcmp(argv[argi], "-q") == 0)
		{
			glQuiet = true;
		}
		else if (memcmp(argv[argi], "-l", 2) == 0)
		{
			meloMaxNoteDist = (int) strtol(&argv[argi][2], &endp, 0);
			if (endp == &argv[argi][2] || meloMaxNoteDist < 1)
			{
				fprintf(stderr, "Error: Option \"%s\" must have a positive number\n", "-l");
				goto finish;
			}
		}
		argi++;
	}
	argc -= argi;
	argv += argi;

	// check number of arguments
	if (argc != 2)
	{
		printUsage();
		goto finish;
	}

	// determine filenames
	strcpy(glInFilename, argv[0]);

	// set MML string
	mml = argv[1];

	// open input file
	inFile = fopen(glInFilename, "rb");
	if (inFile == NULL)
	{
		fprintf(stderr, "Error: Unable to open \"%s\"\n", glInFilename);
		goto finish;
	}

	// start searching notes
	if (!searchNotes(inFile, (const char*) mml, meloMaxNoteDist))
	{
		goto finish;
	}

	ret = EXIT_SUCCESS;

finish:
	if (inFile != NULL)
	{
		fclose(inFile);
	}
	return ret;
}
