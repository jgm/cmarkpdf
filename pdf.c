#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <cmark.h>
#include "cmarkpdf.h"
#include <math.h>
#include <setjmp.h>
#include "hpdf.h"

#define LINE_SPREAD 1.2

jmp_buf env;

#ifdef HPDF_DLL
void  __stdcall
#else
void
#endif
error_handler (HPDF_STATUS   error_no,
               HPDF_STATUS   detail_no,
               void         *user_data)
{
    printf ("ERROR: error_no=%04X, detail_no=%u\n", (HPDF_UINT)error_no,
                (HPDF_UINT)detail_no);
    longjmp(env, 1);
}

struct render_state {
	HPDF_Doc pdf;
	HPDF_Font font;
	HPDF_REAL font_size;
	HPDF_Page page;
	float x;
	float y;
};

static int
S_render_node(cmark_node *node, cmark_event_type ev_type,
              struct render_state *state, int options)
{
	const char *text;
	int entering = ev_type == CMARK_EVENT_ENTER;

	switch (cmark_node_get_type(node)) {
	case CMARK_NODE_DOCUMENT:
		if (entering) {
			/* create default-font */
			state->font = HPDF_GetFont (state->pdf, "Helvetica", NULL);
			state->font_size = 14;

			/* add a new page object. */
			state->page = HPDF_AddPage (state->pdf);
			HPDF_Page_SetFontAndSize (state->page, state->font, state->font_size);

			state->x = 50;
			state->y = HPDF_Page_GetHeight(state->page) - state->font_size;
		}

		break;

	case CMARK_NODE_TEXT:
		printf("y = %f\n", state->y);
		if (state->y < 100) {
			/* add a new page object. */
			state->page = HPDF_AddPage (state->pdf);
			HPDF_Page_SetFontAndSize (state->page, state->font, state->font_size);
			state->y = HPDF_Page_GetHeight(state->page) - state->font_size;
		}
		text = cmark_node_get_literal(node);
		HPDF_Page_BeginText (state->page);
		HPDF_Page_TextOut (state->page, state->x, state->y, text);
		HPDF_Page_EndText (state->page);
		state->x = 50;
		state->y -= (state->font_size * LINE_SPREAD);
		break;

	default:
		break;
	}

	return 1;
}

int cmark_render_pdf(cmark_node *root, int options, char *outfile)
{
	struct render_state state = { };

	state.pdf = HPDF_New (error_handler, NULL);

	if (!state.pdf) {
		printf ("error: cannot create PdfDoc object\n");
		return 1;
	}

	if (HPDF_UseUTFEncodings(state.pdf) != HPDF_OK) {
		printf ("error: cannot set UTF-8 encoding\n");
		return 1;
	};

	if (setjmp(env)) {
		HPDF_Free(state.pdf);
		return 1;
	}

	/* set compression mode */
	HPDF_SetCompressionMode (state.pdf, HPDF_COMP_ALL);


	cmark_event_type ev_type;
	cmark_node *cur;
	cmark_iter *iter = cmark_iter_new(root);

	while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
		cur = cmark_iter_get_node(iter);
		S_render_node(cur, ev_type, &state, options);
	}

	cmark_iter_free(iter);

	/* save the document to a file */
	HPDF_SaveToFile (state.pdf, outfile);

	/* clean up */
	HPDF_Free (state.pdf);


	return 0;
}
