#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct Main;
struct Txt;
struct TxtLine;

#include "lib_compiler_attrs.h"

/* caller must handle `compiled` member. */
void txt_free_lines(struct Txt *txt);
struct Txt *txt_add(struct Main *main, const char *name);
/* Use to a valid UTF-8 sequences.
 * this function replaces extended ascii characters. */
int txt_extended_ascii_as_utf8(char **str);
bool txt_reload(struct Text *text);
/* Load a txt file.
 * param is_internal: If true, this text data-block only exists in memory,
 * not as a file on disk.
 * text data-blocks have no real user but have 'fake user' enabled by default */
struct Txt *txt_load_ex(struct Main *main,
                        const char *file,
                        const char *relpath,
                        bool is_internal);
/* Load a txt file.
 * Txt data-blocks have no user by default, only the 'real user' flag. */
struct Text *txt_load(struct Main *main, const char *file, const char *relpath);
void txt_clear(struct Txt *txt);
void txt_write(struct Txt *txt, const char *str);
/* return codes:
 * -  0 if file on disk is the same or Text is in memory only.
 * -  1 if file has been modified on disk since last local edit.
 * -  2 if file on disk has been deleted.
 * - -1 is returned if an error occurs */
int txt_file_modified_check(struct Txt *txt);
void txt_file_modified_ignore(struct Txt *txt);

char *txt_to_buf(struct Txt *txt, size_t *r_buf_strlen)
    ATTR_NONNULL(1, 2) ATTR_WARN_UNUSED_RESULT ATTR_RETURNS_NONNULL;
void txt_clean_text(struct Txt *text);
void txt_order_cursors(struct Txt *txt, bool reverse);
int txt_find_string(struct Txt *txt, const char *findstr, int wrap, int match_case);
bool txt_has_sel(const struct Txt *txt);
int txt_get_span(struct TxtLine *from, struct TxtLine *to);
void txt_move_up(struct Txt *txt, bool sel);
void txt_move_down(struct Txt *txt, bool sel);
void txt_move_left(struct Txt *txt, bool sel);
void txt_move_right(struct Txt *txt, bool sel);
void txt_jump_left(struct Txt *txt, bool sel, bool use_init_step);
void txt_jump_right(struct Txt *txt, bool sel, bool use_init_step);
void txt_move_bof(struct Txt *txt, bool sel);
void txt_move_eof(struct Txt *txt, bool sel);
void txt_move_bol(struct Txt *txt, bool sel);
void txt_move_eol(struct Txt *txt, bool sel);
void txt_move_toline(struct Txt *txt, unsigned int line, bool sel);
/* Moves to a certain byte in a line, not a certain utf8-character. */
void txt_move_to(struct Txt *txt, unsigned int line, unsigned int ch, bool sel);
void txt_pop_sel(struct Txt *txt);
void txt_delete_char(struct Txt *txt);
void txt_delete_word(struct Txt *txt);
void txt_delete_selected(struct Txt *txt);
void txt_sel_all(struct Txt *txt);
/* Reverse of txt_pop_sel
 * Clears the selection and ensures the cursor is located
 * at the selection (where the cursor is visually while editing). */
void txt_sel_clear(struct Txt *txt);
void txt_sel_line(struct Txt *txt);
void txt_sel_set(struct Txt *txt, int startl, int startc, int endl, int endc);
char *txt_sel_to_buf(struct Txt *txt, size_t *r_buf_strlen);
void txt_insert_buf(struct Txt *txt, const char *in_buffer);
void txt_split_curline(struct Txt *txt);
void txt_backspace_char(struct Txt *txt);
void txt_backspace_word(struct Txt *txt);
bool txt_add_char(struct Txt *txt, unsigned int add);
bool txt_add_raw_char(struct Txt *txt, unsigned int add);
bool txt_replace_char(struct Txt *txt, unsigned int add);
bool txt_unindent(struct Txt *txt);
void txt_comment(struct Txt *txt);
void txt_indent(struct Txt *txt);
bool txt_uncomment(struct Txt *txt);
void txt_move_lines(struct Txt *txt, int direction);
void txt_duplicate_line(struct Txt *txt);
int txt_setcurr_tab_spaces(struct Txt *txt, int space);
bool txt_cursor_is_line_start(const struct Txt *txt);
bool txt_cursor_is_line_end(const struct Txt *txt);

int txt_calc_tab_left(struct TxtLine *tl, int ch);
int txt_calc_tab_right(struct TxtLine *tl, int ch);

/* Util fns, could be moved somewhere more generic but are python/text related. */
int txt_check_bracket(char ch);
bool txt_check_delim(char ch);
bool txt_check_digit(char ch);
bool txt_check_id(char ch);
bool txt_check_id_nodigit(char ch);
bool txt_check_whitespace(char ch);
int txt_find_id_start(const char *str, int i);

/* EVIL: defined in `bpy_interface.c`. */
extern int txt_check_id_unicode(unsigned int ch);
extern int txt_check_id_nodigit_unicode(unsigned int ch);

enum {
  TXT_MOVE_LINE_UP = -1,
  TXT_MOVE_LINE_DOWN = 1,
};

/* Fast non-validating buffer conversion for undo. */
/* Create a buffer, the only requirement is txt_from_buf_for_undo can decode it. */
char *txt_to_buf_for_undo(struct Txt *txt, size_t *r_buf_len)
    ATTR_NONNULL(1, 2) ATTR_WARN_UNUSED_RESULT ATTR_RETURNS_NONNULL;
/* Decode a buffer from txt_to_buf_for_undo. */
void txt_from_buf_for_undo(struct Txt *txt, const char *buf, size_t buf_len) ATTR_NONNULL(1, 2);

#ifdef __cplusplus
}
#endif
