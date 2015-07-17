#ifndef CMARK_CMARK_PDF_H
#define CMARK_CMARK_PDF_H

#include <cmark.h>

#ifdef __cplusplus
extern "C" {
#endif

int cmark_render_pdf(cmark_node *root, int options, char *outfile);

#ifdef __cplusplus
}
#endif

#endif
