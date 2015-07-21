#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <cmark.h>
#include "cmarkpdf.h"
#include <math.h>
#include <setjmp.h>
#include "hpdf.h"

/*
 * TODO
 * links
 * strong, emph
 * list markers for ordered lists (also should use different
   bullets for nested bullet lists)
 * images
 * make PDF searchable and copy-pasteable
   (I'm having the problem described here, https://groups.google.com/forum/#!searchin/libharu/copy$20paste/libharu/YzXoH_K3OAI/YNCsn6XXF-gJ)
  note also http://superuser.com/questions/137824/pdf-has-garbled-text-when-copy-pasting
  I only have the problem in PReview, not in Adobe Reader or Chrome's PDF viewer.
  So it may be a Preview issue.
*/


// MAIN_FONT_PATH defined in Makefile
// TT_FONT_PATH defined in Makefile
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

enum box_type {
	TEXT,
	SPACE,
	BREAK
};

struct box {
	enum box_type type;
	const char * text;
	int len;
	HPDF_Font font;
	float width;
	struct box * next;
};

typedef struct box box;

/*
// for diagnostics
static void
print_box(box * box)
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
*/

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
	float last_text_y;
	box * boxes_bottom;
	box * boxes_top;
};

static void
push_box(struct render_state *state,
	 enum box_type type,
	 const char * text,
	 HPDF_Font font)
{
	HPDF_TextWidth width;
	box * new = (box*)malloc(sizeof(box));
	if (new == NULL)
		return;
	new->type = type;
	new->text = text;
	new->len = strlen(text);
	new->font = font;
	width = HPDF_Font_TextWidth(font, (HPDF_BYTE*)text, new->len);
	new->width = ( width.width * state->current_font_size ) / 1000;
	if (new->type == SPACE) {
		// spaces have minimum width reduced to aid justification
		new->width -= (state->current_font_size / 16);
	}
	new->next = NULL;
	if (state->boxes_top != NULL) {
		state->boxes_top->next = new;
	}
	state->boxes_top = new;
	if (state->boxes_bottom == NULL) {
		state->boxes_bottom = new;
	}
}

static void
render_text(struct render_state *state, HPDF_Font font, const char *text, bool wrap)
{
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
			last_tok = next;
			push_box(state, tok[0] == ' ' ?
				      SPACE : (tok[0] == '\n' ?
					       BREAK : TEXT), tok, font);
			// free(tok);
		}
		if (*next == 0)
			break;
		last_category = category;
		next++;
	}

}

static void
render_box(struct render_state *state, box * b)
{
	if (state->y < HPDF_Page_GetHeight(state->page) -
	    TEXT_HEIGHT) {
		/* add a new page object. */
		state->page = HPDF_AddPage (state->pdf);
		state->y = HPDF_Page_GetHeight(state->page) - MARGIN_TOP;
	}
	HPDF_Page_SetFontAndSize (state->page, b->font, state->current_font_size);
	if (b->type == SPACE) {
		state->x += b->width;
	} else {
		HPDF_Page_BeginText (state->page);
		HPDF_Page_MoveTextPos(state->page, state->x, state->y);
		HPDF_Page_ShowText(state->page, b->text);
		HPDF_Page_EndText (state->page);
		state->x += b->width;
	}
}

static void
process_boxes(struct render_state *state, HPDF_Font font, bool wrap)
{
	box *b;
	box *tmp;
	box *last_nonspace;
	float total_width = 0;
	float extra_space_width;
	float line_end_space;
	float max_width = TEXT_WIDTH - state->indent;
	int numspaces;
	int numspaces_to_last_nonspace;

	while (state->boxes_bottom) {

		numspaces = 0;
		b = state->boxes_bottom;
		// move forward to last box that can fit in line
		while (b &&
		       b->type != BREAK &&
		       (!wrap || total_width + b->width <= max_width)) {
			total_width += b->width;
			if (b->type == SPACE) {
				numspaces++;
			} else {
				last_nonspace = b;
				numspaces_to_last_nonspace = numspaces;
			}
			b = b->next;
		}

		// recalculate space widths, unless last line of para.
		if (b && wrap) {
			line_end_space = max_width - total_width;
			extra_space_width = (line_end_space / numspaces_to_last_nonspace);
		} else { // last line
			extra_space_width = state->current_font_size / 10;
		}

		tmp = state->boxes_bottom;
		if (wrap) {
			while (tmp && tmp != last_nonspace) {
				if (tmp->type == SPACE) {
					tmp->width += extra_space_width;
				}
				tmp = tmp->next;
			}
		}

		// emit line up to last_nonspace;

		// remove and free everything up to last_nonspace,
		// plus any following spaces. reset boxes_bottom.
		total_width = 0;
		while (state->boxes_bottom &&
		       (state->boxes_bottom != last_nonspace->next)) {
			render_box(state, state->boxes_bottom);
			tmp = state->boxes_bottom->next;
			// free((char *)state->boxes_bottom->text);
			free(state->boxes_bottom);
			state->boxes_bottom = tmp;
		}
		//gobble spaces
		while (state->boxes_bottom && state->boxes_bottom->type == SPACE) {
			tmp = state->boxes_bottom->next;
			// free(state->boxes_bottom->text);
			free(state->boxes_bottom);
			state->boxes_bottom = tmp;
		}
		//gobble at most one BREAK
		if (state->boxes_bottom && state->boxes_bottom->type == BREAK) {
			tmp = state->boxes_bottom->next;
			// free(state->boxes_bottom->text);
			free(state->boxes_bottom);
			state->boxes_bottom = tmp;
		}

		state->last_text_y = state->y;
		state->x = MARGIN_LEFT + state->indent;
		state->y -= (state->current_font_size + state->leading);

	}
	state->boxes_top = NULL;
	state->boxes_bottom = NULL;

}

static void
parbreak(struct render_state *state)
{
	state->y = state->last_text_y -
		(1.5 * (state->current_font_size + state->leading));
	state->x = MARGIN_LEFT + state->indent;
}

static int
S_render_node(cmark_node *node, cmark_event_type ev_type,
              struct render_state *state, int options)
{
	int entering = ev_type == CMARK_EVENT_ENTER;
	const char *main_font;
	const char *tt_font;
	HPDF_TextWidth width;
	float real_width;

	switch (cmark_node_get_type(node)) {
	case CMARK_NODE_DOCUMENT:
		if (entering) {
			state->base_font_size = 10;
			state->current_font_size = 10;
			state->leading = 4;
			state->indent = 0;
			state->boxes_bottom = NULL;
			state->boxes_top = NULL;

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

			/* add a new page object. */
			state->page = HPDF_AddPage (state->pdf);
			HPDF_Page_SetFontAndSize (state->page, state->main_font, state->current_font_size);

			state->x = MARGIN_LEFT + state->indent;
			state->y = HPDF_Page_GetHeight(state->page) - MARGIN_TOP;
			state->last_text_y = state->y;
		}

		break;

	case CMARK_NODE_ITEM:
		width = HPDF_Font_TextWidth(state->main_font, (HPDF_BYTE*)"\xE2\x80\xA2  ", 5);
		real_width = ( width.width * state->current_font_size ) / 1000;
		if (entering) {
			parbreak(state);
			HPDF_Page_BeginText (state->page);
			HPDF_Page_MoveTextPos(state->page, state->x, state->y);
			HPDF_Page_ShowText(state->page, "\xE2\x80\xA2  ");
			HPDF_Page_EndText (state->page);
			state->indent += real_width;
			state->x = MARGIN_LEFT + state->indent;
		} else {
			state->indent -= real_width;
		}
		break;

	case CMARK_NODE_HRULE:
		parbreak(state);
		HPDF_Page_MoveTo(state->page, state->x, state->y + state->leading);
		HPDF_Page_LineTo(state->page, state->x + (TEXT_WIDTH - (state->x - MARGIN_LEFT)), state->y + state->leading);
		HPDF_Page_Stroke(state->page);
		state->last_text_y = state->y;
		state->y -= (state->current_font_size + state->leading);
		state->x = MARGIN_LEFT + state->indent;
		break;

	case CMARK_NODE_BLOCK_QUOTE:
		if (entering) {
			// TODO also change font size, right indent?
			state->indent += state->current_font_size * 2;
		} else {
			state->indent -= state->current_font_size * 2;
		}
		break;

	case CMARK_NODE_PARAGRAPH:
		if (entering) {
			parbreak(state);
		} else {
			process_boxes(state, state->main_font, true);
		}
		break;

	case CMARK_NODE_CODE_BLOCK:
		parbreak(state);
		render_text(state, state->tt_font, cmark_node_get_literal(node), false);
		process_boxes(state, state->tt_font, false);
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
			process_boxes(state, state->main_font, true);
			state->y -= (0.3 * (state->current_font_size + state->leading));
			state->current_font_size = state->base_font_size;
			HPDF_Page_SetFontAndSize (state->page,
						  state->main_font,
						  state->current_font_size);
		}
		break;

	case CMARK_NODE_CODE:
		render_text(state, state->tt_font, cmark_node_get_literal(node), false);
		break;

	case CMARK_NODE_SOFTBREAK:
		push_box(state, TEXT, " ", state->main_font);
		break;

	case CMARK_NODE_LINEBREAK:
		push_box(state, BREAK, "\n", state->main_font);
		break;

	case CMARK_NODE_TEXT:
		render_text(state, state->main_font, cmark_node_get_literal(node), true);
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
