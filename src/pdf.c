#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <cmark.h>
#include "cmarkpdf.h"
#include <math.h>
#include "hpdf.h"


// MAIN_FONT_PATH defined in Makefile
// TT_FONT_PATH defined in Makefile
#define MARGIN_TOP 80
#define MARGIN_LEFT 80
#define TEXT_WIDTH 420
#define TEXT_HEIGHT 760

#define STATUS_OK 1
#define STATUS_ERR 0

#define errf(fmt, args) \
	fprintf(stderr, "ERROR (%s:%d): ", __FILE__, __LINE__); \
	fprintf(stderr, fmt, args); \
	fprintf(stderr, "\n"); \
	return STATUS_ERR;

#define err(msg) errf("%s", msg);

#ifdef HPDF_DLL
void  __stdcall
#else
void
#endif
error_handler (HPDF_STATUS   error_no,
               HPDF_STATUS   detail_no,
               void         *user_data)
{
    printf ("ERROR: error_no=%04X, detail_no=%u\n",
	    (HPDF_UINT)error_no, (HPDF_UINT)detail_no);
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
	float width;
	struct box * next;
	bool bold;
	bool italic;
	bool monospace;
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
	int list_indent_level;
};

static int
push_box(struct render_state *state,
	 enum box_type type,
	 const char * text,
	 bool monospace,
	 bool bold,
	 bool italic)
{
	HPDF_TextWidth width;
	HPDF_Font font;

	font = monospace ? state->tt_font : state->main_font;
	box * new = (box*)malloc(sizeof(box));
	new->monospace = monospace;
	new->bold = bold;
	new->italic = italic;
	if (new == NULL) {
		err("Could not allocate box");
	}
	new->type = type;
	new->text = text;
	new->len = text ? strlen(text) : 0;
	if (new->type == SPACE) {
		width = HPDF_Font_TextWidth(font, (HPDF_BYTE*)"i", 1);
		if (!monospace) {
			width.width *= 0.67;
		}
	} else {
		width = HPDF_Font_TextWidth(font, (HPDF_BYTE*)text, new->len);
	}
	new->width = ( width.width * state->current_font_size ) / 1000;
	new->next = NULL;
	if (state->boxes_top != NULL) {
		state->boxes_top->next = new;
	}
	state->boxes_top = new;
	if (state->boxes_bottom == NULL) {
		state->boxes_bottom = new;
	}
	return STATUS_OK;
}

static int
render_text(struct render_state *state, const char *text, bool wrap,
	    bool monospace, bool bold, bool italic)
{
	char * tok;
	const char * next = text;
	const char * last_tok = text;
	int category = 0;
	int last_category = 0;
	int status;

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
			if (tok == NULL) {
				err("Could not allocate token");
			}
			memcpy(tok, last_tok, next - last_tok);
			tok[next - last_tok] = 0;
			last_tok = next;
			status = push_box(state, tok[0] == ' ' ?
				      SPACE : (tok[0] == '\n' ?
					       BREAK : TEXT), tok,
					  monospace, bold, italic);
			if (status == STATUS_ERR) {
				return STATUS_ERR;
			}
		}
		if (*next == 0)
			break;
		last_category = category;
		next++;
	}
	return STATUS_OK;
}

static int
add_page_if_needed(struct render_state *state)
{
	if (!state->page ||
	    state->y < HPDF_Page_GetHeight(state->page) - TEXT_HEIGHT) {
		/* add a new page object. */
		state->page = HPDF_AddPage (state->pdf);
		if (!state->page) {
			err("Could not add page");
		}
		state->y = HPDF_Page_GetHeight(state->page) - MARGIN_TOP;
		state->x = MARGIN_LEFT + state->indent;
		state->last_text_y = state->y;
	}
	HPDF_Page_SetFontAndSize (state->page, state->main_font, state->current_font_size);
	return STATUS_OK;
}

static int
render_box(struct render_state *state, box * b)
{
	int status;
	HPDF_Font font;

	font = b->monospace ? state->tt_font : state->main_font;

	status = add_page_if_needed(state);
	if (status == STATUS_ERR) {
		return status;
	}
	HPDF_Page_SetFontAndSize (state->page, font, state->current_font_size);
	if (b->type == SPACE) {
		state->x += b->width;
	} else {
		HPDF_Page_BeginText (state->page);
		HPDF_Page_MoveTextPos(state->page, state->x, state->y);
		HPDF_Page_ShowText(state->page, b->text);
		HPDF_Page_EndText (state->page);
		state->x += b->width;
	}
	return STATUS_OK;
}

static void
process_boxes(struct render_state *state, bool wrap)
{
	box *b;
	box *tmp;
	box *last_nonspace;
	box *stop;
	float total_width = 0;
	float extra_space_width;
	float line_end_space;
	float max_width = TEXT_WIDTH - state->indent;
	int numspaces;
	int numspaces_to_last_nonspace;

	while (state->boxes_bottom) {

		numspaces = 0;
		b = state->boxes_bottom;
		last_nonspace = b;
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
		stop = last_nonspace->next;
		while (state->boxes_bottom &&
		       (state->boxes_bottom != stop)) {
			render_box(state, state->boxes_bottom);
			tmp = state->boxes_bottom;
			state->boxes_bottom = state->boxes_bottom->next;
			if (tmp->text) {
				free((char*)tmp->text);
			}
			free(tmp);
		}
		//gobble spaces
		while (state->boxes_bottom && state->boxes_bottom->type == SPACE) {
			tmp = state->boxes_bottom;
			state->boxes_bottom = state->boxes_bottom->next;
			if (tmp->text) {
				free((char*)tmp->text);
			}
			free(tmp);
		}
		//gobble at most one BREAK
		if (state->boxes_bottom && state->boxes_bottom->type == BREAK) {
			tmp = state->boxes_bottom;
			state->boxes_bottom = state->boxes_bottom->next;
			if (tmp->text) {
				free((char*)tmp->text);
			}
			free(tmp);
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
	add_page_if_needed(state);
	state->y = state->last_text_y -
		(1.5 * (state->current_font_size + state->leading));
	state->x = MARGIN_LEFT + state->indent;
}

static int
S_render_node(cmark_node *node, cmark_event_type ev_type,
              struct render_state *state, int options)
{
	int status;
	int entering = ev_type == CMARK_EVENT_ENTER;
	const char *main_font;
	const char *tt_font;
	float real_width;
	char * bullets[] = {"\xE2\x97\xA6",
			    "\xE2\x80\xA2"};
	char marker[20];
	size_t len;
	cmark_node * parent;

	switch (cmark_node_get_type(node)) {
	case CMARK_NODE_DOCUMENT:
		if (entering) {
			state->base_font_size = 10;
			state->current_font_size = 10;
			state->leading = 4;
			state->indent = 0;
			state->boxes_bottom = NULL;
			state->boxes_top = NULL;
			state->list_indent_level = 0;

			main_font = HPDF_LoadTTFontFromFile(state->pdf,
							   MAIN_FONT_PATH,
							   HPDF_TRUE);
			if (!main_font) {
				errf("Could not load main font '%s'",
				    MAIN_FONT_PATH);
			}
			state->main_font = HPDF_GetFont (state->pdf,
							 main_font,
							 "UTF-8");
			if (!state->main_font) {
				errf("Could not get main font '%s'",
				    main_font);
			}

			tt_font = HPDF_LoadTTFontFromFile(state->pdf,
							   TT_FONT_PATH,
							   HPDF_TRUE);
			if (!tt_font) {
				errf("Could not load monospace font '%s'",
				    TT_FONT_PATH);
			}
			state->tt_font = HPDF_GetFont (state->pdf,
						       tt_font,
						       "UTF-8");
			if (!state->tt_font) {
				errf("Could not get monospace font '%s'",
				    tt_font);
			}

		}

		break;

	case CMARK_NODE_ITEM:
		parent = cmark_node_parent(node);
		real_width = state->current_font_size *
			(cmark_node_get_list_type(parent) == CMARK_BULLET_LIST ?
			 1.5 : 3);
		if (entering) {
			if (cmark_node_get_list_type(parent) == CMARK_BULLET_LIST) {
				len = strlen(bullets[state->list_indent_level % 2]);
				memcpy(marker, bullets[state->list_indent_level % 2], len);
				marker[len] = 0;
			} else {
				sprintf(marker, "%4d.", cmark_node_get_list_start(parent));
				len = strlen(marker);
			}
			parbreak(state);
			HPDF_Page_BeginText (state->page);
			HPDF_Page_MoveTextPos(state->page, state->x, state->y);
			HPDF_Page_ShowText(state->page, marker);
			HPDF_Page_EndText (state->page);
			state->x += real_width;
			state->indent += real_width;
		} else {
			state->indent -= real_width;
		}
		break;

	case CMARK_NODE_LIST:
		if (entering) {
			state->list_indent_level++;
		} else {
			state->list_indent_level--;
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
			process_boxes(state, true);
		}
		break;

	case CMARK_NODE_CODE_BLOCK:
		parbreak(state);
		status = render_text(state, cmark_node_get_literal(node), false, true, false, false);
		if (status == STATUS_ERR) {
			return STATUS_ERR;
		}
		process_boxes(state, false);
		HPDF_Page_SetFontAndSize (state->page, state->main_font, state->current_font_size);
		state->y -= (state->current_font_size + state->leading);
		return STATUS_OK;

	case CMARK_NODE_HEADER:
		if (entering) {
			int lev = cmark_node_get_header_level(node);
			state->current_font_size = state->base_font_size * (1.66 - (lev/6));
			HPDF_Page_SetFontAndSize (state->page,
						  state->main_font,
						  state->current_font_size);
			parbreak(state);
		} else {
			process_boxes(state, true);
			state->y -= (0.3 * (state->current_font_size + state->leading));
			state->current_font_size = state->base_font_size;
			HPDF_Page_SetFontAndSize (state->page,
						  state->main_font,
						  state->current_font_size);
		}
		break;

	case CMARK_NODE_CODE:
		return render_text(state, cmark_node_get_literal(node), false, true, false, false);

	case CMARK_NODE_SOFTBREAK:
		return push_box(state, SPACE, NULL, false, false, false);

	case CMARK_NODE_LINEBREAK:
		return push_box(state, BREAK, NULL, false, false, false);

	case CMARK_NODE_TEXT:
		return render_text(state, cmark_node_get_literal(node), true, false, false, false);

	default:
		break;
	}

	return STATUS_OK;
}

// Returns 1 on success, 0 on failure.
int cmark_render_pdf(cmark_node *root, int options, char *outfile)
{
	struct render_state state = { };

	state.pdf = HPDF_New (error_handler, NULL);

	if (!state.pdf) {
		err("Cannot create PdfDoc object");
	}

	if (HPDF_UseUTFEncodings(state.pdf) != HPDF_OK) {
		err("Cannot set UTF-8 encoding");
	};

	/* set compression mode */
	HPDF_SetCompressionMode (state.pdf, HPDF_COMP_ALL);


	cmark_event_type ev_type;
	cmark_node *cur;
	cmark_iter *iter = cmark_iter_new(root);
	int status = STATUS_OK;

	while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
		cur = cmark_iter_get_node(iter);
		status = S_render_node(cur, ev_type, &state, options);
		if (status == STATUS_ERR) {
			break;
		}
	}

	cmark_iter_free(iter);

	if (status == STATUS_OK) {
		/* save the document to a file */
		if (HPDF_SaveToFile (state.pdf, outfile) != HPDF_OK) {
			errf("Could not save PDF to file '%s'", outfile);
			status = STATUS_ERR;
		}
	}

	/* clean up */
	HPDF_Free (state.pdf);

	return status;
}
