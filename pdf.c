#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <cmark.h>
#include "cmarkpdf.h"
#include <math.h>
#include <setjmp.h>
#include "hpdf.h"

#define MAIN_FONT_PATH "/Library/Fonts/Georgia.ttf"
#define TT_FONT_PATH "/Library/Fonts/Andale Mono.ttf"
#define MARGIN_TOP 100
#define MARGIN_LEFT 50
#define TEXT_WIDTH 500
#define TEXT_HEIGHT 750

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
	HPDF_Font main_font;
	HPDF_Font tt_font;
	HPDF_REAL font_size;
	HPDF_REAL leading;
	HPDF_Page page;
	float x;
	float y;
};

void
S_render_text(struct render_state *state, HPDF_Font font, const char *text)
{
	HPDF_TextWidth width;
	int len = strlen(text);
	float real_width;

	if (text && text[0] == ' ') {
	 	state->x += (state->font_size / 6);
	}
	width = HPDF_Font_TextWidth(font, (const HPDF_BYTE*)text, len);
	real_width = ( width.width * state->font_size ) / 1000;
	if (state->x + real_width > MARGIN_LEFT + TEXT_WIDTH) {
		state->x = MARGIN_LEFT;
		state->y -= (state->font_size + state->leading);
	}
	HPDF_Page_BeginText (state->page);
	HPDF_Page_MoveTextPos(state->page,
			      state->x, state->y);
	HPDF_Page_ShowText(state->page,
			   text);
	HPDF_Page_EndText (state->page);
	state->x += real_width;
	if (len > 1 && text[len - 1] == ' ') {
	 	state->x += (state->font_size / 6);
	}
}

static int
S_render_node(cmark_node *node, cmark_event_type ev_type,
              struct render_state *state, int options)
{
	const char *text;
	int entering = ev_type == CMARK_EVENT_ENTER;
	const char *main_font;
	const char *tt_font;

	switch (cmark_node_get_type(node)) {
	case CMARK_NODE_DOCUMENT:
		if (entering) {
			main_font = HPDF_LoadTTFontFromFile(state->pdf,
							   MAIN_FONT_PATH,
							   HPDF_TRUE);
			state->main_font = HPDF_GetFont (state->pdf,
							 main_font,
							 "UTF-8");
			tt_font = HPDF_LoadTTFontFromFile(state->pdf,
							   TT_FONT_PATH,
							   HPDF_TRUE);
			state->tt_font = HPDF_GetFont (state->pdf,
						       tt_font,
						       "UTF-8");
			state->font_size = 12;
			state->leading = 4;

			/* add a new page object. */
			state->page = HPDF_AddPage (state->pdf);
			HPDF_Page_SetFontAndSize (state->page, state->main_font, state->font_size);

			state->x = MARGIN_LEFT;
			state->y = HPDF_Page_GetHeight(state->page) - MARGIN_TOP;
		}

		break;

	case CMARK_NODE_PARAGRAPH:
		if (entering) {
			state->y -= (1.5 * (state->font_size + state->leading));
			state->x = MARGIN_LEFT;
		}
		break;

	case CMARK_NODE_HEADER:
		if (entering) {
			state->y -= (2.5 * (state->font_size + state->leading));
			state->x = MARGIN_LEFT;
			int lev = cmark_node_get_header_level(node);
			HPDF_Page_SetFontAndSize (state->page,
						  state->main_font,
						  state->font_size *
						  (1.66 - (lev / 6)));
		} else {
			state->y -= (0.3 * (state->font_size + state->leading));
			HPDF_Page_SetFontAndSize (state->page,
						  state->main_font,
						  state->font_size);
		}
		break;

	case CMARK_NODE_CODE:
		HPDF_Page_SetFontAndSize (state->page,
					  state->tt_font,
					  state->font_size);

		S_render_text(state, state->tt_font, cmark_node_get_literal(node));
		HPDF_Page_SetFontAndSize (state->page,
					  state->main_font,
					  state->font_size);
		break;

	case CMARK_NODE_SOFTBREAK:
		state->x += (state->font_size / 6);
		break;

	case CMARK_NODE_TEXT:
		if (state->y < HPDF_Page_GetHeight(state->page) -
		    TEXT_HEIGHT) {
			/* add a new page object. */
			state->page = HPDF_AddPage (state->pdf);
			HPDF_Page_SetFontAndSize (state->page, state->main_font, state->font_size);
			state->y = HPDF_Page_GetHeight(state->page) - MARGIN_TOP;
		}
		text = cmark_node_get_literal(node);
		S_render_text(state, state->main_font, text);
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
