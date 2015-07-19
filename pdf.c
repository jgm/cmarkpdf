#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <cmark.h>
#include "cmarkpdf.h"
#include <math.h>
#include <setjmp.h>
#include "hpdf.h"

/*
 * TODO
 * For a better approach, have the inline renderers add to a list
 * of InlineElements.  Each of these has full information about
 * text content (which might be a space), minimum width,
 * hyperlink destination,
 * image information.
 * Also "command directives" like "turn on bold" or
 * "turn off italic."
 * Regular spaces and hard breaks will also be InlineElements.
 * Then have a routine that renders a list of these, splitting
 * them intelligently into lines (Knuth/Prass or something simpler).
 * Start by implementing this for regular TEXT, CODE, LINE_BREAK,
 * SOFT_BREAK, and spaces. Note that the renderer for TEXT will
 * have to break on spaces and insert appropriate space elements.
*/


#define MAIN_FONT_PATH "/Library/Fonts/Georgia.ttf"
#define TT_FONT_PATH "/Library/Fonts/Andale Mono.ttf"
#define MARGIN_TOP 80
#define MARGIN_LEFT 80
#define TEXT_WIDTH 420
#define TEXT_HEIGHT 760

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

enum text_box_type {
	TEXT,
	SPACE,
	BREAK
};

struct text_box {
	enum text_box_type type;
	const char * text;
	float width;
	struct text_box * next;
};

static void
print_text_box(struct text_box * box)
{
	switch (box->type) {
	case TEXT:
		printf("TEXT  ");
		break;
	case SPACE:
		printf("SPACE ");
		break;
	case BREAK:
		printf("BREAK ");
		break;
	default:
		break;
	}
	printf("%5.2f|%s|\n", box->width, box->text);
};

struct render_state {
	HPDF_Doc pdf;
	HPDF_Font main_font;
	HPDF_Font tt_font;
	HPDF_REAL base_font_size;
	HPDF_REAL current_font_size;
	HPDF_REAL leading;
	HPDF_Page page;
	float indent;
	float x;
	float y;
	struct text_box * text_list_bottom;
	struct text_box * text_list_top;
};

static void
push_text_box(struct render_state *state,
	      enum text_box_type type,
	      const char * text,
	      float width)
{
	struct text_box * new =
		(struct text_box*)malloc(sizeof(struct text_box));
	if (new == NULL)
		return;
	new->type = type;
	new->text = text;
	new->width = width;
	new->next = NULL;
	if (state->text_list_top != NULL) {
		state->text_list_top->next = new;
	}
	state->text_list_top = new;
	if (state->text_list_bottom == NULL) {
		state->text_list_bottom = new;
	}
}

static void
render_text(struct render_state *state, HPDF_Font font, const char *text, int wrap)
{
	HPDF_TextWidth width;
	int len;
	float real_width;
	char * tok;
	const char * next = text;
	const char * last_tok = text;
	int category = 0;
	int last_category = 0;

	while (1) {
		switch (*next) {
		case ' ':
			category = 1;
			break;
		case '\n':
			category = 2;
			break;
		case 0:
			category = 0;
			break;
		default:
			category = 3;
		}
		if (category != last_category && next > last_tok) {
			// emit token from last_tok to next-1
			tok = (char *)malloc((next - last_tok) + 1);
			memcpy(tok, last_tok, next - last_tok);
			tok[next - last_tok] = 0;
			len = next - last_tok;
			// printf("token: |%s| len %d\n", tok, len);
			if (tok[0] == '\n') {
				state->x = MARGIN_LEFT + state->indent;
				state->y -= strlen(tok) * (state->current_font_size + state->leading);
			} else {
				width = HPDF_Font_TextWidth(font, (const HPDF_BYTE*)tok, len);
				real_width = ( width.width * state->current_font_size ) / 1000;
				if (wrap && state->x + real_width > MARGIN_LEFT + state->indent + TEXT_WIDTH) {
					state->x = MARGIN_LEFT + state->indent;
					state->y -= (state->current_font_size + state->leading);
				}
				if (state->y < HPDF_Page_GetHeight(state->page) -
				    TEXT_HEIGHT) {
					/* add a new page object. */
					state->page = HPDF_AddPage (state->pdf);
					HPDF_Page_SetFontAndSize (state->page, state->main_font, state->current_font_size);
					state->y = HPDF_Page_GetHeight(state->page) - MARGIN_TOP;
				}
				HPDF_Page_BeginText (state->page);
				HPDF_Page_MoveTextPos(state->page, state->x, state->y);
				HPDF_Page_ShowText(state->page, tok);
				HPDF_Page_EndText (state->page);
				state->x += real_width;
			}

			last_tok = next;
			push_text_box(state, tok[0] == ' ' ?
				      SPACE : (tok[0] == '\n' ?
					       BREAK : TEXT), tok, real_width);
			// free(tok);
		}
		if (*next == 0)
			break;
		last_category = category;
		next++;
	}

}

static void
parbreak(struct render_state *state)
{
	if (state->x > MARGIN_LEFT + state->indent) {
		state->y -= (1.5 * (state->current_font_size + state->leading));
		state->x = MARGIN_LEFT + state->indent;
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
	struct text_box *box;

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
			state->base_font_size = 12;
			state->current_font_size = 12;
			state->leading = 4;
			state->indent = 0;
			state->text_list_bottom = NULL;
			state->text_list_top = NULL;

			/* add a new page object. */
			state->page = HPDF_AddPage (state->pdf);
			HPDF_Page_SetFontAndSize (state->page, state->main_font, state->current_font_size);

			state->x = MARGIN_LEFT + state->indent;
			state->y = HPDF_Page_GetHeight(state->page) - MARGIN_TOP;
		}

		break;

	case CMARK_NODE_ITEM:
		if (entering) {
			parbreak(state);
			HPDF_Page_BeginText (state->page);
			HPDF_Page_MoveTextPos(state->page, state->x, state->y);
			HPDF_Page_ShowText(state->page, "-");
			HPDF_Page_EndText (state->page);
			state->indent += 24;
			state->x = MARGIN_LEFT + state->indent;
		} else {
			state->indent -= 24;
		}
		break;

	case CMARK_NODE_PARAGRAPH:
		if (entering) {
			parbreak(state);
		} else {
			box = state->text_list_bottom;
			while (box) {
				print_text_box(box);
				box = box->next;
			}
		}
		break;

	case CMARK_NODE_CODE_BLOCK:
		parbreak(state);
		HPDF_Page_SetFontAndSize (state->page, state->tt_font, state->current_font_size);
		render_text(state, state->tt_font, cmark_node_get_literal(node), 0);
		HPDF_Page_SetFontAndSize (state->page, state->main_font, state->current_font_size);
		state->y -= (state->current_font_size + state->leading);
		break;

	case CMARK_NODE_HEADER:
		if (entering) {
			int lev = cmark_node_get_header_level(node);
			state->current_font_size = state->base_font_size * (1.66 - (lev/6));
			HPDF_Page_SetFontAndSize (state->page,
						  state->main_font,
						  state->current_font_size);
			parbreak(state);
		} else {
			state->y -= (0.3 * (state->current_font_size + state->leading));
			state->current_font_size = state->base_font_size;
			HPDF_Page_SetFontAndSize (state->page,
						  state->main_font,
						  state->current_font_size);
		}
		break;

	case CMARK_NODE_CODE:
		HPDF_Page_SetFontAndSize (state->page,
					  state->tt_font,
					  state->current_font_size);

		render_text(state, state->tt_font, cmark_node_get_literal(node), 1);
		HPDF_Page_SetFontAndSize (state->page,
					  state->main_font,
					  state->current_font_size);
		break;

	case CMARK_NODE_SOFTBREAK:
		render_text(state, state->main_font, " ", 1);
		break;

	case CMARK_NODE_TEXT:
		text = cmark_node_get_literal(node);
		render_text(state, state->main_font, text, 1);
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
