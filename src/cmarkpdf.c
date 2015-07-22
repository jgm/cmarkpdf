#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
#include <errno.h>
#include <cmark.h>
#include <hpdf.h>
#include "cmarkpdf.h"

#if defined(_WIN32) && !defined(__CYGWIN__)
#include <io.h>
#include <fcntl.h>
#endif

void print_usage()
{
	printf("Usage:   cmarkpdf [FILE*]\n");
	printf("Options:\n");
	printf("  --output, -o FILE Output filename\n");
	printf("  --sourcepos       Include source position attribute\n");
	printf("  --hardbreaks      Treat newlines as hard line breaks\n");
	printf("  --smart           Use smart punctuation\n");
	printf("  --help, -h        Print usage information\n");
	printf("  --version         Print version\n");
}

static void print_document(cmark_node *document, char *outfile,
                           int options, int width)
{
	int result;
	result = cmark_render_pdf(document, options, outfile);
}

int main(int argc, char *argv[])
{
	int i, numfps = 0;
	int *files;
	char buffer[4096];
	cmark_parser *parser;
	size_t bytes;
	cmark_node *document;
	int width = 0;
	char *outfile = NULL;
	int options = CMARK_OPT_DEFAULT | CMARK_OPT_SAFE | CMARK_OPT_NORMALIZE;

#if defined(_WIN32) && !defined(__CYGWIN__)
	_setmode(_fileno(stdout), _O_BINARY);
#endif

	files = (int *)malloc(argc * sizeof(*files));

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--version") == 0) {
			printf("cmark %s", CMARK_VERSION_STRING);
			printf(" - CommonMark converter\n(C) 2014, 2015 John MacFarlane\n");
			exit(0);
		} else if (strcmp(argv[i], "--sourcepos") == 0) {
			options |= CMARK_OPT_SOURCEPOS;
		} else if (strcmp(argv[i], "--hardbreaks") == 0) {
			options |= CMARK_OPT_HARDBREAKS;
		} else if (strcmp(argv[i], "--smart") == 0) {
			options |= CMARK_OPT_SMART;
		} else if (strcmp(argv[i], "--validate-utf8") == 0) {
			options |= CMARK_OPT_VALIDATE_UTF8;
		} else if ((strcmp(argv[i], "--help") == 0) ||
		           (strcmp(argv[i], "-h") == 0)) {
			print_usage();
			exit(0);
		} else if ((strcmp(argv[i], "-o") == 0) ||
		           (strcmp(argv[i], "--output") == 0)) {
			i += 1;
			if (i < argc) {
				outfile = argv[i];
			} else {
				fprintf(stderr, "No argument provided for %s\n",
				        argv[i - 1]);
				exit(1);
			}
		} else if (*argv[i] == '-') {
			print_usage();
			exit(1);
		} else { // treat as file argument
			files[numfps++] = i;
		}
	}

	if (!outfile) {
		fprintf(stderr, "Specify an output file with -o/--output\n");
		exit(1);
	}

	parser = cmark_parser_new(options);
	for (i = 0; i < numfps; i++) {
		FILE *fp = fopen(argv[files[i]], "r");
		if (fp == NULL) {
			fprintf(stderr, "Error opening file %s: %s\n",
			        argv[files[i]], strerror(errno));
			exit(1);
		}

		while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
			cmark_parser_feed(parser, buffer, bytes);
			if (bytes < sizeof(buffer)) {
				break;
			}
		}

		fclose(fp);
	}

	if (numfps == 0) {

		while ((bytes = fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
			cmark_parser_feed(parser, buffer, bytes);
			if (bytes < sizeof(buffer)) {
				break;
			}
		}
	}

	document = cmark_parser_finish(parser);
	cmark_parser_free(parser);

	print_document(document, outfile, options, width);

	cmark_node_free(document);

	free(files);

	return 0;
}
