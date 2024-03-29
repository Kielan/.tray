#include <stdlib.h> /* abort */
#include <string.h> /* strstr */
#include <sys/stat.h>
#include <sys/types.h>
#include <wctype.h>

#include "mem_guardedalloc.h"

#include "lib_fileops.h"
#include "lib_list.h"
#include "lib_path_util.h"
#include "lib_string.h"
#include "lib_string_cursor_utf8.h"
#include "lib_string_utf8.h"
#include "lib_utildefines.h"

#include "lang.h"

#include "types_constraint.h"
#include "types_material.h"
#include "types_node.h"
#include "types_object.h"
#include "types_scene.h"
#include "types_screen.h"
#include "types_space.h"
#include "types_text.h"
#include "types_userdef.h"

#include "tray_path.h"
#include "tray_idtype.h"
#include "tray_lib_id.h"
#include "tray_main.h"
#include "tray_node.h"
#include "tray_txt.h"

#include "loader_read_write.h"

/* Prototypes **/
static void txt_pop_first(Txt *txt);
static void txt_pop_last(Txt *txt);
static void txt_delete_line(Txt *txt, TxtLine *line);
static void txt_delete_sel(Txt *txt);
static void txt_make_dirty(Txt *txt);

/* Txt Data-Block */
static void txt_init_data(Id *id)
{
  Txt *txt = (Txt *)id;
  TxtLine *tmp;

  lib_assert(MEMCMP_STRUCT_AFTER_IS_ZERO(txt, id));

  txt->filepath = NULL;

  txt->flags = TXT_ISDIRTY | TXT_ISMEM;
  if ((U.flag & USER_TXT_TABSTOSPACES_DISABLE) == 0) {
    txt->flags |= TXT_TABSTOSPACES;
  }

  lib_list_clear(&txt->lines);

  tmp = (TxtLine *)mem_mallocn(sizeof(TxtLine), "txtline");
  tmp->line = (char *)mem_mallocn(1, "txtline_string");
  tmp->format = NULL;

  tmp->line[0] = 0;
  tmp->len = 0;

  tmp->next = NULL;
  tmp->prev = NULL;

  lib_addhead(&txt->lines, tmp);

  txt->curl = txt->lines.first;
  txt->curc = 0;
  txt->sell = txt->lines.first;
  txt->selc = 0;
}

/* Only copy internal data of Text Id from source
 * to already allocated/initialized destination.
 * You probably never want to use that directly,
 * use dune_id_copy or dune_id_copy_ex for typical needs.
 *
 * WARNING! This fn will not handle Id user count!
 *
 * param flag: Copying options (see dune_lib_id.h's LIB_ID_COPY_... flags for more). */
static void txt_copy_data(Main *UNUSED(main),
                          Id *id_dst,
                          const Id *id_src,
                          const int UNUSED(flag))
{
  Txt *txt_dst = (Txt *)id_dst;
  const Txt *txt_src = (Txt *)id_src;

  /* File name can be NULL. */
  if (txt_src->filepath) {
    txt_dst->filepath = lib_strdup(txt_src->filepath);
  }

  txt_dst->flags |= TXT_ISDIRTY;

  lib_list_clear(&txt_dst->lines);
  txt_dst->curl = txt_dst->sell = NULL;
  txt_dst->compiled = NULL;

  /* Walk down, reconstructing. */
  LIST_FOREACH (TxtLine *, line_src, &text_src->lines) {
    TxtLine *line_dst = mem_mallocn(sizeof(*line_dst), __func__);

    line_dst->line = lib_strdup(line_src->line);
    line_dst->format = NULL;
    line_dst->len = line_src->len;

    lib_addtail(&txt_dst->lines, line_dst);
  }

  txt_dst->curl = txt_dst->sell = txt_dst->lines.first;
  txt_dst->curc = txt_dst->selc = 0;
}

/* Free (or release) any data used by this txt (does not free the txt itself). */
static void txt_free_data(Id *id)
{
  /* No animation-data here. */
  Txt *txt = (Txt *)id;

  txt_free_lines(txt);

  MEM_SAFE_FREE(txt->filepath);
}

static void txt_foreach_path(Id *id, PathForeachPathData *path_data)
{
  Txt *txt = (Txt *)id;

  if (txt->filepath != NULL) {
    path_foreach_path_allocated_process(path_data, &txt->filepath);
  }
}

static void txt_tray_write(Writer *writer, Id *id, const void *id_address)
{
  Txt *txt = (Txt *)id;

  /* NOTE: we are clearing local temp data here, *not* the flag in the actual 'real' ID. */
  if ((txt->flags & TXT_ISMEM) && (txt->flags & TXT_ISEXT)) {
    txt->flags &= ~TXT_ISEXT;
  }

  /* Clean up, important in undo case to reduce false detection of changed datablocks. */
  txt->compiled = NULL;

  /* write LibData */
  loader_write_id_struct(writer, Txt, id_address, &txt->id);
  id_write(writer, &text->id);

  if (txt->filepath) {
    loader_write_string(writer, txt->filepath);
  }

  if (!(txt->flags & TXT_ISEXT)) {
    /* now write the text data, in two steps for optimization in the readfunction */
    LIST_FOREACH (TxtLine *, tmp, &txt->lines) {
      loader_write_struct(writer, TxtLine, tmp);
    }

    LIST_FOREACH (TxtLine *, tmp, &txt->lines) {
     loader_write_raw(writer, tmp->len + 1, tmp->line);
    }
  }
}

static void txt_read_data(DataReader *reader, Id *id)
{
  Txt *txt = (Txt *)id;
  loader_read_data_address(reader, &txt->filepath);

  txt->compiled = NULL;

#if 0
  if (txt->flags & TXT_ISEXT) {
    txt_reload(txt);
  }
  /* else { */
#endif

  loader_read_list(reader, &txt->lines);

  loader_read_data_address(reader, &txt->curl);
  loader_read_data_address(reader, &txt->sell);

  LIST_FOREACH (TxtLine *, ln, &txt->lines) {
    loader_read_data_address(reader, &ln->line);
    ln->format = NULL;

    if (ln->len != (int)strlen(ln->line)) {
      printf("Error loading text, line lengths differ\n");
      ln->len = strlen(ln->line);
    }
  }

  txt->flags = (txt->flags) & ~TXT_ISEXT;
}

IdTypeInfo IdType_ID_TXT = {
    .id_code = ID_TXT,
    .id_filter = FILTER_ID_TXT,
    .main_list_index = INDEX_ID_TXT,
    .struct_size = sizeof(Text),
    .name = "Text",
    .name_plural = "texts",
    .lang_cxt = LANG_CXT_ID_TEXT,
    .flags = IDTYPE_FLAGS_NO_ANIMDATA | IDTYPE_FLAGS_APPEND_IS_REUSABLE,
    .asset_type_info = NULL,

    .init_data = txt_init_data,
    .copy_data = txt_copy_data,
    .free_data = txt_free_data,
    .make_local = NULL,
    .foreach_id = NULL,
    .foreach_cache = NULL,
    .foreach_path = txt_foreach_path,
    .owner_get = NULL,

    .tray_write = txt_write,
    .tray_read_data = txt_read_data,
    .tray_read_lib = NULL,
    .tray_read_expand = NULL,

    .tray_read_undo_preserve = NULL,

    .lib_override_apply_post = NULL,
};

/* Txt Add, Free, Validation */
void txt_free_lines(Txt *txt)
{
  for (TxtLine *tmp = txt->lines.first, *tmp_next; tmp; tmp = tmp_next) {
    tmp_next = tmp->next;
    mem_freen(tmp->line);
    if (tmp->format) {
      mem_freen(tmp->format);
    }
    mem_freen(tmp);
  }

  lib_list_clear(&text->lines);

  text->curl = text->sell = NULL;
}

Text *txt_add(Main *main, const char *name)
{
  Txt *ta;

  ta = id_new(main, ID_TXT, name);
  /* Texts have no users by default... Set the fake user flag to ensure that this text block
   * doesn't get deleted by default when cleaning up data blocks. */
  id_us_min(&ta->id);
  id_fake_user_set(&ta->id);

  return ta;
}

int txt_extended_ascii_as_utf8(char **str)
{
  ptrdiff_t bad_char, i = 0;
  const ptrdiff_t length = (ptrdiff_t)strlen(*str);
  int added = 0;

  while ((*str)[i]) {
    if ((bad_char = lib_str_utf8_invalid_byte(*str + i, length - i)) == -1) {
      break;
    }

    added++;
    i += bad_char + 1;
  }

  if (added != 0) {
    char *newstr = mem_mallocn(length + added + 1, "text_line");
    ptrdiff_t mi = 0;
    i = 0;

    while ((*str)[i]) {
      if ((bad_char = lib_str_utf8_invalid_byte((*str) + i, length - i)) == -1) {
        memcpy(newstr + mi, (*str) + i, length - i + 1);
        break;
      }

      memcpy(newstr + mi, (*str) + i, bad_char);

      const int mofs = mi + bad_char;
      lib_str_utf8_from_unicode((*str)[i + bad_char], newstr + mofs, (length + added) - mofs);
      i += bad_char + 1;
      mi += bad_char + 2;
    }
    newstr[length + added] = '\0';
    mem_freen(*str);
    *str = newstr;
  }

  return added;
}

/* Removes any control characters from a txt-line and fixes invalid UTF8 sequences. */
static void cleanup_txtline(TxtLine *tl)
{
  int i;

  for (i = 0; i < tl->len; i++) {
    if (tl->line[i] < ' ' && tl->line[i] != '\t') {
      memmove(tl->line + i, tl->line + i + 1, tl->len - i);
      tl->len--;
      i--;
    }
  }
  tl->len += txt_extended_ascii_as_utf8(&tl->line);
}

/* used for load and reload (unlike txt_insert_buf)
 * assumes all fields are empty */
static void txt_from_buf(Text *text, const unsigned char *buffer, const int len)
{
  int i, llen, lines_count;

  lib_assert(lib_list_is_empty(&text->lines));

  llen = 0;
  lines_count = 0;
  for (i = 0; i < len; i++) {
    if (buffer[i] == '\n') {
      TextLine *tmp;

      tmp = (TextLine *)mem_mallocn(sizeof(TextLine), "textline");
      tmp->line = (char *)mem_mallocn(llen + 1, "textline_string");
      tmp->format = NULL;

      if (llen) {
        memcpy(tmp->line, &buffer[i - llen], llen);
      }
      tmp->line[llen] = 0;
      tmp->len = llen;

      cleanup_textline(tmp);

      lib_addtail(&text->lines, tmp);
      lines_count += 1;

      llen = 0;
      continue;
    }
    llen++;
  }

  /* create new line in cases:
   * - rest of line (if last line in file hasn't got \n terminator).
   *   in this case content of such line would be used to fill text line buffer
   * - file is empty. in this case new line is needed to start editing from.
   * - last character in buffer is \n. in this case new line is needed to
   *   deal with newline at end of file. (see T28087) */
  if (llen != 0 || lines_count == 0 || buffer[len - 1] == '\n') {
    TxtLine *tmp;

    tmp = (TxtLine *)mem_mallocn(sizeof(TxtLine), "txtline");
    tmp->line = (char *)mem_mallocn(llen + 1, "txtline_string");
    tmp->format = NULL;

    if (llen) {
      memcpy(tmp->line, &buffer[i - llen], llen);
    }

    tmp->line[llen] = 0;
    tmp->len = llen;

    cleanup_textline(tmp);

    lib_addtail(&txt->lines, tmp);
    /* lines_count += 1; */ /* UNUSED */
  }

  txt->curl = txt->sell = txt->lines.first;
  txt->curc = txt->selc = 0;
}

bool txt_reload(Txt *txt)
{
  unsigned char *buffer;
  size_t buffer_len;
  char filepath_abs[FILE_MAX];
  lib_stat_t st;

  if (!txt->filepath) {
    return false;
  }

  lib_strncpy(filepath_abs, txt->filepath, FILE_MAX);
  lib_path_abs(filepath_abs, ID_PATH_FROM_GLOBAL(&text->id));

  buffer = lib_file_read_txt_as_mem(filepath_abs, 0, &buffer_len);
  if (buffer == NULL) {
    return false;
  }

  /* free memory: */
  txt_free_lines(txt);
  txt_make_dirty(txt);

  /* clear undo buffer */
  if (lib_stat(filepath_abs, &st) != -1) {
    txt->mtime = st.st_mtime;
  }
  else {
    txt->mtime = 0;
  }

  txt_from_buf(text, buffer, buffer_len);

  mem_freen(buffer);
  return true;
}

Txt *txt_load_ex(Main *main, const char *file, const char *relpath, const bool is_internal)
{
  unsigned char *buffer;
  size_t buffer_len;
  Txt *ta;
  char filepath_abs[FILE_MAX];
  lib_stat_t st;

  lib_strncpy(filepath_abs, file, FILE_MAX);
  if (relpath) { /* Can be NULL (background mode). */
    lib_path_abs(filepath_abs, relpath);
  }

  buffer = lib_file_read_txt_as_mem(filepath_abs, 0, &buffer_len);
  if (buffer == NULL) {
    return NULL;
  }

  ta = libblock_alloc(dunemain, ID_TXT, lib_path_basename(filepath_abs), 0);
  id_us_min(&ta->id);
  id_fake_user_set(&ta->id);

  lib_list_clear(&ta->lines);
  ta->curl = ta->sell = NULL;

  if ((U.flag & USER_TXT_TABSTOSPACES_DISABLE) == 0) {
    ta->flags = TXT_TABSTOSPACES;
  }

  if (is_internal == false) {
    ta->filepath = mem_mallocn(strlen(file) + 1, "text_name");
    strcpy(ta->filepath, file);
  }
  else {
    ta->flags |= TXT_ISMEM | TXT_ISDIRTY;
  }

  /* clear undo buffer */
  if (lib_stat(filepath_abs, &st) != -1) {
    ta->mtime = st.st_mtime;
  }
  else {
    ta->mtime = 0;
  }

  txt_from_buf(ta, buffer, buffer_len);

  mem_freen(buffer);

  return ta;
}

Txt *txt_load(Main *main, const char *file, const char *relpath)
{
  return txt_load_ex(main, file, relpath, false);
}

void txt_clear(Txt *txt) /* called directly from api */
{
  txt_sel_all(txt);
  txt_delete_sel(txt);
  txt_make_dirty(txt);
}

void txt_write(Txt *txt, const char *str) /* called directly from api */
{
  txt_insert_buf(txt, str);
  txt_move_eof(txt, 0);
  txt_make_dirty(txt);
}

int txt_file_modified_check(Txt *txt)
{
  lib_stat_t st;
  int result;
  char file[FILE_MAX];

  if (!txt->filepath) {
    return 0;
  }

  lib_strncpy(file, txt->filepath, FILE_MAX);
  lib_path_abs(file, ID_PATH_FROM_GLOBAL(&txt->id));

  if (!lib_exists(file)) {
    return 2;
  }

  result = lib_stat(file, &st);

  if (result == -1) {
    return -1;
  }

  if ((st.st_mode & S_IFMT) != S_IFREG) {
    return -1;
  }

  if (st.st_mtime > txt->mtime) {
    return 1;
  }

  return 0;
}

void txt_file_modified_ignore(Txt *txt)
{
  lib_stat_t st;
  int result;
  char file[FILE_MAX];

  if (!text->filepath) {
    return;
  }

  lib_strncpy(file, text->filepath, FILE_MAX);
  lib_path_abs(file, ID_PATH_FROM_GLOBAL(&text->id));

  if (!lib_exists(file)) {
    return;
  }

  result = lib_stat(file, &st);

  if (result == -1 || (st.st_mode & S_IFMT) != S_IFREG) {
    return;
  }

  text->mtime = st.st_mtime;
}

/* Editing Util Fns */
static void make_new_line(TxtLine *line, char *newline)
{
  if (line->line) {
    mem_freen(line->line);
  }
  if (line->format) {
    mem_freen(line->format);
  }

  line->line = newline;
  line->len = strlen(newline);
  line->format = NULL;
}

static TxtLine *txt_new_line(const char *str)
{
  TxtLine *tmp;

  if (!str) {
    str = "";
  }

  tmp = (TxtLine *)mem_mallocn(sizeof(TxtLine), "txtline");
  tmp->line = mem_mallocn(strlen(str) + 1, "txtline_string");
  tmp->format = NULL;

  strcpy(tmp->line, str);

  tmp->len = strlen(str);
  tmp->next = tmp->prev = NULL;

  return tmp;
}

static TxtLine *txt_new_linen(const char *str, int n)
{
  TxtLine *tmp;

  tmp = (TxtLine *)mem_mallocn(sizeof(TxtLine), "txtline");
  tmp->line = mem_mallocn(n + 1, "txtline_string");
  tmp->format = NULL;

  lib_strncpy(tmp->line, (str) ? str : "", n + 1);

  tmp->len = strlen(tmp->line);
  tmp->next = tmp->prev = NULL;

  return tmp;
}

void txt_clean_text(Txt *txt)
{
  TxtLine **top, **bot;

  if (!txt->lines.first) {
    if (txt->lines.last) {
      txt->lines.first = txt->lines.last;
    }
    else {
      txt->lines.first = txt->lines.last = txt_new_line(NULL);
    }
  }

  if (!txt->lines.last) {
    txt->lines.last = txt->lines.first;
  }

  top = (TxtLine **)&txt->lines.first;
  bot = (TxtLine **)&txt->lines.last;

  while ((*top)->prev) {
    *top = (*top)->prev;
  }
  while ((*bot)->next) {
    *bot = (*bot)->next;
  }

  if (!txt->curl) {
    if (txt->sell) {
      txt->curl = txt->sell;
    }
    else {
      txt->curl = txt->lines.first;
    }
    txt->curc = 0;
  }

  if (!txt->sell) {
    txt->sell = txt->curl;
    txt->selc = 0;
  }
}

int txt_get_span(TxtLine *from, TxtLine *to)
{
  int ret = 0;
  TxtLine *tmp = from;

  if (!to || !from) {
    return 0;
  }
  if (from == to) {
    return 0;
  }

  /* Look forwards */
  while (tmp) {
    if (tmp == to) {
      return ret;
    }
    ret++;
    tmp = tmp->next;
  }

  /* Look backwards */
  if (!tmp) {
    tmp = from;
    ret = 0;
    while (tmp) {
      if (tmp == to) {
        break;
      }
      ret--;
      tmp = tmp->prev;
    }
    if (!tmp) {
      ret = 0;
    }
  }

  return ret;
}

static void txt_make_dirty(Txt *txt)
{
  txt->flags |= TXT_ISDIRTY;
}

/* Cursor Util Fns */
static void txt_curs_cur(Txt *txt, TxtLine ***linep, int **charp)
{
  *linep = &txt->curl;
  *charp = &txt->curc;
}

static void txt_curs_sel(Txt *txt, TxtLine ***linep, int **charp)
{
  *linep = &txt->sell;
  *charp = &txt->selc;
}

bool txt_cursor_is_line_start(const Txt *txt)
{
  return (txt->selc == 0);
}

bool txt_cursor_is_line_end(const Txt *txt)
{
  return (txt->selc == txt->sell->len);
}

/* Cursor Movement Fns
 *
 * note If the user moves the cursor the space containing that cursor should be popped
 * See txt_pop_first, txt_pop_last
 * Other space-types retain their own top location. */
void txt_move_up(Txt *txt, const bool sel)
{
  TxtLine **linep;
  int *charp;

  if (sel) {
    txt_curs_sel(txt, &linep, &charp);
  }
  else {
    txt_pop_first(txt);
    txt_curs_cur(txt, &linep, &charp);
  }
  if (!*linep) {
    return;
  }

  if ((*linep)->prev) {
    int column = lib_str_utf8_offset_to_column((*linep)->line, *charp);
    *linep = (*linep)->prev;
    *charp = lib_str_utf8_offset_from_column((*linep)->line, column);
  }
  else {
    txt_move_bol(txt, sel);
  }

  if (!sel) {
    txt_pop_sel(txt);
  }
}

void txt_move_down(Txt *txt, const bool sel)
{
  TxtLine **linep;
  int *charp;

  if (sel) {
    txt_curs_sel(txt, &linep, &charp);
  }
  else {
    txt_pop_last(txt);
    txt_curs_cur(txt, &linep, &charp);
  }
  if (!*linep) {
    return;
  }

  if ((*linep)->next) {
    int column = lib_str_utf8_offset_to_column((*linep)->line, *charp);
    *linep = (*linep)->next;
    *charp = lib_str_utf8_offset_from_column((*linep)->line, column);
  }
  else {
    txt_move_eol(txt, sel);
  }

  if (!sel) {
    txt_pop_sel(txt);
  }
}

int txt_calc_tab_left(TxtLine *tl, int ch)
{
  /* do nice left only if there are only spaces */
  int tabsize = (ch < TXT_TABSIZE) ? ch : TXT_TABSIZE;

  for (int i = 0; i < ch; i++) {
    if (tl->line[i] != ' ') {
      tabsize = 0;
      break;
    }
  }

  /* if in the middle of the space-tab */
  if (tabsize && ch % TXT_TABSIZE != 0) {
    tabsize = (ch % TXT_TABSIZE);
  }
  return tabsize;
}

int txt_calc_tab_right(TextLine *tl, int ch)
{
  if (tl->line[ch] == ' ') {
    int i;
    for (i = 0; i < ch; i++) {
      if (tl->line[i] != ' ') {
        return 0;
      }
    }

    int tabsize = (ch) % TXT_TABSIZE + 1;
    for (i = ch + 1; tl->line[i] == ' ' && tabsize < TXT_TABSIZE; i++) {
      tabsize++;
    }

    return i - ch;
  }

  return 0;
}

void txt_move_left(Txt *txt, const bool sel)
{
  TxtLine **linep;
  int *charp;
  int tabsize = 0;

  if (sel) {
    txt_curs_sel(txt, &linep, &charp);
  }
  else {
    txt_pop_first(txt);
    txt_curs_cur(txt, &linep, &charp);
  }
  if (!*linep) {
    return;
  }

  if (*charp == 0) {
    if ((*linep)->prev) {
      txt_move_up(txt, sel);
      *charp = (*linep)->len;
    }
  }
  else {
    /* do nice left only if there are only spaces */
    /* TXT_TABSIZE hard-coded in structs_text_types.h */
    if (text->flags & TXT_TABSTOSPACES) {
      tabsize = txt_calc_tab_left(*linep, *charp);
    }

    if (tabsize) {
      (*charp) -= tabsize;
    }
    else {
      const char *prev = lib_str_find_prev_char_utf8((*linep)->line + *charp, (*linep)->line);
      *charp = prev - (*linep)->line;
    }
  }

  if (!sel) {
    txt_pop_sel(text);
  }
}

void txt_move_right(Txt *txt, const bool sel)
{
  TxtLine **linep;
  int *charp;

  if (sel) {
    txt_curs_sel(txt, &linep, &charp);
  }
  else {
    txt_pop_last(txt);
    txt_curs_cur(text, &linep, &charp);
  }
  if (!*linep) {
    return;
  }

  if (*charp == (*linep)->len) {
    if ((*linep)->next) {
      txt_move_down(txt, sel);
      *charp = 0;
    }
  }
  else {
    /* do nice right only if there are only spaces */
    /* spaces hardcoded in DNA_text_types.h */
    int tabsize = 0;

    if (txt->flags & TXT_TABSTOSPACES) {
      tabsize = txt_calc_tab_right(*linep, *charp);
    }

    if (tabsize) {
      (*charp) += tabsize;
    }
    else {
      (*charp) += lib_str_utf8_size((*linep)->line + *charp);
    }
  }

  if (!sel) {
    txt_pop_sel(text);
  }
}

void txt_jump_left(Txt *txt, const bool sel, const bool use_init_step)
{
  TxtLine **linep;
  int *charp;

  if (sel) {
    txt_curs_sel(text, &linep, &charp);
  }
  else {
    txt_pop_first(txt);
    txt_curs_cur(txt, &linep, &charp);
  }
  if (!*linep) {
    return;
  }

  lib_str_cursor_step_utf8(
      (*linep)->line, (*linep)->len, charp, STRCUR_DIR_PREV, STRCUR_JUMP_DELIM, use_init_step);

  if (!sel) {
    txt_pop_sel(text);
  }
}

void txt_jump_right(Txt *txt, const bool sel, const bool use_init_step)
{
  TxtLine **linep;
  int *charp;

  if (sel) {
    txt_curs_sel(txt, &linep, &charp);
  }
  else {
    txt_pop_last(txt);
    txt_curs_cur(txt, &linep, &charp);
  }
  if (!*linep) {
    return;
  }

  lib_str_cursor_step_utf8(
      (*linep)->line, (*linep)->len, charp, STRCUR_DIR_NEXT, STRCUR_JUMP_DELIM, use_init_step);

  if (!sel) {
    txt_pop_sel(text);
  }
}

void txt_move_bol(Txt *txt, const bool sel)
{
  TxtLine **linep;
  int *charp;

  if (sel) {
    txt_curs_sel(txt, &linep, &charp);
  }
  else {
    txt_curs_cur(txt, &linep, &charp);
  }
  if (!*linep) {
    return;
  }

  *charp = 0;

  if (!sel) {
    txt_pop_sel(txt);
  }
}

void txt_move_eol(Txt *txt, const bool sel)
{
  TxtLine **linep;
  int *charp;

  if (sel) {
    txt_curs_sel(txt, &linep, &charp);
  }
  else {
    txt_curs_cur(txt, &linep, &charp);
  }
  if (!*linep) {
    return;
  }

  *charp = (*linep)->len;

  if (!sel) {
    txt_pop_sel(text);
  }
}

void txt_move_bof(Txt *txt, const bool sel)
{
  TxtLine **linep;
  int *charp;

  if (sel) {
    txt_curs_sel(text, &linep, &charp);
  }
  else {
    txt_curs_cur(text, &linep, &charp);
  }
  if (!*linep) {
    return;
  }

  *linep = text->lines.first;
  *charp = 0;

  if (!sel) {
    txt_pop_sel(text);
  }
}

void txt_move_eof(Text *text, const bool sel)
{
  TextLine **linep;
  int *charp;

  if (sel) {
    txt_curs_sel(text, &linep, &charp);
  }
  else {
    txt_curs_cur(text, &linep, &charp);
  }
  if (!*linep) {
    return;
  }

  *linep = text->lines.last;
  *charp = (*linep)->len;

  if (!sel) {
    txt_pop_sel(text);
  }
}

void txt_move_toline(Text *text, unsigned int line, const bool sel)
{
  txt_move_to(text, line, 0, sel);
}

void txt_move_to(Text *text, unsigned int line, unsigned int ch, const bool sel)
{
  TextLine **linep;
  int *charp;
  unsigned int i;

  if (sel) {
    txt_curs_sel(text, &linep, &charp);
  }
  else {
    txt_curs_cur(text, &linep, &charp);
  }
  if (!*linep) {
    return;
  }

  *linep = text->lines.first;
  for (i = 0; i < line; i++) {
    if ((*linep)->next) {
      *linep = (*linep)->next;
    }
    else {
      break;
    }
  }
  if (ch > (unsigned int)((*linep)->len)) {
    ch = (unsigned int)((*linep)->len);
  }
  *charp = ch;

  if (!sel) {
    txt_pop_sel(text);
  }
}

/* Text Selection Fns */
static void txt_curs_swap(Text *text)
{
  TextLine *tmpl;
  int tmpc;

  tmpl = text->curl;
  text->curl = text->sell;
  text->sell = tmpl;

  tmpc = text->curc;
  text->curc = text->selc;
  text->selc = tmpc;
}

static void txt_pop_first(Text *text)
{
  if (txt_get_span(text->curl, text->sell) < 0 ||
      (text->curl == text->sell && text->curc > text->selc)) {
    txt_curs_swap(text);
  }

  txt_pop_sel(text);
}

static void txt_pop_last(Text *text)
{
  if (txt_get_span(text->curl, text->sell) > 0 ||
      (text->curl == text->sell && text->curc < text->selc)) {
    txt_curs_swap(text);
  }

  txt_pop_sel(text);
}

void txt_pop_sel(Text *text)
{
  text->sell = text->curl;
  text->selc = text->curc;
}

void txt_order_cursors(Text *text, const bool reverse)
{
  if (!text->curl) {
    return;
  }
  if (!text->sell) {
    return;
  }

  /* Flip so text->curl is before/after text->sell */
  if (reverse == false) {
    if ((txt_get_span(text->curl, text->sell) < 0) ||
        (text->curl == text->sell && text->curc > text->selc)) {
      txt_curs_swap(text);
    }
  }
  else {
    if ((txt_get_span(text->curl, text->sell) > 0) ||
        (text->curl == text->sell && text->curc < text->selc)) {
      txt_curs_swap(text);
    }
  }
}

bool txt_has_sel(const Text *text)
{
  return ((text->curl != text->sell) || (text->curc != text->selc));
}

static void txt_delete_sel(Text *text)
{
  TextLine *tmpl;
  char *buf;

  if (!text->curl) {
    return;
  }
  if (!text->sell) {
    return;
  }

  if (!txt_has_sel(text)) {
    return;
  }

  txt_order_cursors(text, false);

  buf = mem_mallocn(text->curc + (text->sell->len - text->selc) + 1, "textline_string");

  strncpy(buf, text->curl->line, text->curc);
  strcpy(buf + text->curc, text->sell->line + text->selc);
  buf[text->curc + (text->sell->len - text->selc)] = 0;

  make_new_line(text->curl, buf);

  tmpl = text->sell;
  while (tmpl != text->curl) {
    tmpl = tmpl->prev;
    if (!tmpl) {
      break;
    }

    txt_delete_line(text, tmpl->next);
  }

  text->sell = text->curl;
  text->selc = text->curc;
}

void txt_sel_all(Text *text)
{
  text->curl = text->lines.first;
  text->curc = 0;

  text->sell = text->lines.last;
  text->selc = text->sell->len;
}

void txt_sel_clear(Text *text)
{
  if (text->sell) {
    text->curl = text->sell;
    text->curc = text->selc;
  }
}

void txt_sel_line(Text *text)
{
  if (!text->curl) {
    return;
  }

  text->curc = 0;
  text->sell = text->curl;
  text->selc = text->sell->len;
}

void txt_sel_set(Text *text, int startl, int startc, int endl, int endc)
{
  TextLine *froml, *tol;
  int fromllen, tollen;

  /* Support negative indices. */
  if (startl < 0 || endl < 0) {
    int end = lib_list_count(&text->lines) - 1;
    if (startl < 0) {
      startl = end + startl + 1;
    }
    if (endl < 0) {
      endl = end + endl + 1;
    }
  }
  CLAMP_MIN(startl, 0);
  CLAMP_MIN(endl, 0);

  froml = lib_findlink(&text->lines, startl);
  if (froml == NULL) {
    froml = text->lines.last;
  }
  if (startl == endl) {
    tol = froml;
  }
  else {
    tol = lib_findlink(&text->lines, endl);
    if (tol == NULL) {
      tol = text->lines.last;
    }
  }

  fromllen = lib_strlen_utf8(froml->line);
  tollen = lib_strlen_utf8(tol->line);

  /* Support negative indices. */
  if (startc < 0) {
    startc = fromllen + startc + 1;
  }
  if (endc < 0) {
    endc = tollen + endc + 1;
  }

  CLAMP(startc, 0, fromllen);
  CLAMP(endc, 0, tollen);

  text->curl = froml;
  text->curc = lib_str_utf8_offset_from_index(froml->line, startc);
  text->sell = tol;
  text->selc = lib_str_utf8_offset_from_index(tol->line, endc);
}

/* Buffer Conversion for Undo/Redo
 *
 * Buffer conversion functions that rely on the buffer already being validated.
 *
 * The only requirement for these functions is that they're reverse-able,
 * the undo logic doesn't inspect their content.
 *
 * Currently buffers:
 * - Always ends with a new-line.
 * - Are not null terminated. */

char *txt_to_buf_for_undo(Text *text, size_t *r_buf_len)
{
  int buf_len = 0;
  LIST_FOREACH (const TextLine *, l, &text->lines) {
    buf_len += l->len + 1;
  }
  char *buf = mem_mallocn(buf_len, __func__);
  char *buf_step = buf;
  LIST_FOREACH (const TextLine *, l, &text->lines) {
    memcpy(buf_step, l->line, l->len);
    buf_step += l->len;
    *buf_step++ = '\n';
  }
  *r_buf_len = buf_len;
  return buf;
}

void txt_from_buf_for_undo(Text *text, const char *buf, size_t buf_len)
{
  const char *buf_end = buf + buf_len;
  const char *buf_step = buf;

  /* First re-use existing lines.
   * Good for undo since it means in practice many operations re-use all
   * except for the modified line. */
  TextLine *l_src = text->lines.first;
  lib_list_clear(&text->lines);
  while (buf_step != buf_end && l_src) {
    /* New lines are ensured by txt_to_buf_for_undo. */
    const char *buf_step_next = strchr(buf_step, '\n');
    const int len = buf_step_next - buf_step;

    TextLine *l = l_src;
    l_src = l_src->next;
    if (l->len != len) {
      l->line = mem_reallocn(l->line, len + 1);
      l->len = len;
    }
    MEM_SAFE_FREE(l->format);

    memcpy(l->line, buf_step, len);
    l->line[len] = '\0';
    lib_addtail(&text->lines, l);
    buf_step = buf_step_next + 1;
  }

  /* If we have extra lines. */
  while (l_src != NULL) {
    TextLine *l_src_next = l_src->next;
    mem_freen(l_src->line);
    if (l_src->format) {
      mem_freen(l_src->format);
    }
    mem_freen(l_src);
    l_src = l_src_next;
  }

  while (buf_step != buf_end) {
    /* New lines are ensured by #txt_to_buf_for_undo. */
    const char *buf_step_next = strchr(buf_step, '\n');
    const int len = buf_step_next - buf_step;

    TextLine *l = mem_mallocn(sizeof(TextLine), "textline");
    l->line = mem_mallocn(len + 1, "textline_string");
    l->len = len;
    l->format = NULL;

    memcpy(l->line, buf_step, len);
    l->line[len] = '\0';
    lib_addtail(&text->lines, l);
    buf_step = buf_step_next + 1;
  }

  text->curl = text->sell = text->lines.first;
  text->curc = text->selc = 0;

  txt_make_dirty(text);
}

/* Cut and Paste Fns **/
char *txt_to_buf(Text *text, size_t *r_buf_strlen)
{
  /* Identical to #txt_to_buf_for_undo except that the string is nil terminated. */
  size_t buf_len = 0;
  LIST_FOREACH (const TextLine *, l, &text->lines) {
    buf_len += l->len + 1;
  }
  char *buf = mem_mallocn(buf_len + 1, __func__);
  char *buf_step = buf;
  LIST_FOREACH (const TextLine *, l, &text->lines) {
    memcpy(buf_step, l->line, l->len);
    buf_step += l->len;
    *buf_step++ = '\n';
  }
  *buf_step = '\0';
  *r_buf_strlen = buf_len;
  return buf;
}

char *txt_sel_to_buf(Text *text, size_t *r_buf_strlen)
{
  char *buf;
  size_t length = 0;
  TextLine *tmp, *linef, *linel;
  int charf, charl;

  if (r_buf_strlen) {
    *r_buf_strlen = 0;
  }

  if (!text->curl) {
    return NULL;
  }
  if (!text->sell) {
    return NULL;
  }

  if (text->curl == text->sell) {
    linef = linel = text->curl;

    if (text->curc < text->selc) {
      charf = text->curc;
      charl = text->selc;
    }
    else {
      charf = text->selc;
      charl = text->curc;
    }
  }
  else if (txt_get_span(text->curl, text->sell) < 0) {
    linef = text->sell;
    linel = text->curl;

    charf = text->selc;
    charl = text->curc;
  }
  else {
    linef = text->curl;
    linel = text->sell;

    charf = text->curc;
    charl = text->selc;
  }

  if (linef == linel) {
    length = charl - charf;
    buf = mem_mallocn(length + 1, "sel buffer");
    memcpy(buf, linef->line + charf, length);
    buf[length] = '\0';
  }
  else {
    /* Add 1 for the '\n' */
    length = (linef->len - charf) + charl + 1;

    for (tmp = linef->next; tmp && tmp != linel; tmp = tmp->next) {
      length += tmp->len + 1;
    }

    buf = mem_mallocn(length + 1, "sel buffer");

    memcpy(buf, linef->line + charf, linef->len - charf);
    length = linef->len - charf;
    buf[length++] = '\n';

    for (tmp = linef->next; tmp && tmp != linel; tmp = tmp->next) {
      memcpy(buf + length, tmp->line, tmp->len);
      length += tmp->len;
      buf[length++] = '\n';
    }

    memcpy(buf + length, linel->line, charl);
    length += charl;
    buf[length] = '\0';
  }

  if (r_buf_strlen) {
    *r_buf_strlen = length;
  }

  return buf;
}

void txt_insert_buf(Text *text, const char *in_buffer)
{
  int l = 0, len;
  size_t i = 0, j;
  TextLine *add;
  char *buffer;

  if (!in_buffer) {
    return;
  }

  txt_delete_sel(text);

  len = strlen(in_buffer);
  buffer = lib_strdupn(in_buffer, len);
  len += txt_extended_ascii_as_utf8(&buffer);

  /* Read the first line (or as close as possible */
  while (buffer[i] && buffer[i] != '\n') {
    txt_add_raw_char(text, lib_str_utf8_as_unicode_step(buffer, len, &i));
  }

  if (buffer[i] == '\n') {
    txt_split_curline(text);
    i++;

    while (i < len) {
      l = 0;

      while (buffer[i] && buffer[i] != '\n') {
        i++;
        l++;
      }

      if (buffer[i] == '\n') {
        add = txt_new_linen(buffer + (i - l), l);
        lib_insertlinkbefore(&text->lines, text->curl, add);
        i++;
      }
      else {
        for (j = i - l; j < i && j < len;) {
          txt_add_raw_char(text, LIB_str_utf8_as_unicode_step(buffer, len, &j));
        }
        break;
      }
    }
  }

  mem_freen(buffer);
}

/* Find String in Text */
int txt_find_string(Text *text, const char *findstr, int wrap, int match_case)
{
  TextLine *tl, *startl;
  const char *s = NULL;

  if (!text->curl || !text->sell) {
    return 0;
  }

  txt_order_cursors(text, false);

  tl = startl = text->sell;

  if (match_case) {
    s = strstr(&tl->line[text->selc], findstr);
  }
  else {
    s = lib_strcasestr(&tl->line[text->selc], findstr);
  }
  while (!s) {
    tl = tl->next;
    if (!tl) {
      if (wrap) {
        tl = text->lines.first;
      }
      else {
        break;
      }
    }

    if (match_case) {
      s = strstr(tl->line, findstr);
    }
    else {
      s = lib_strcasestr(tl->line, findstr);
    }
    if (tl == startl) {
      break;
    }
  }

  if (s) {
    int newl = txt_get_span(text->lines.first, tl);
    int newc = (int)(s - tl->line);
    txt_move_to(text, newl, newc, 0);
    txt_move_to(text, newl, newc + strlen(findstr), 1);
    return 1;
  }

  return 0;
}

/* Line Editing Fns **/
void txt_split_curline(Text *text)
{
  TextLine *ins;
  char *left, *right;

  if (!text->curl) {
    return;
  }

  txt_delete_sel(text);

  /* Make the two half strings */
  left = mem_mallocn(text->curc + 1, "textline_string");
  if (text->curc) {
    memcpy(left, text->curl->line, text->curc);
  }
  left[text->curc] = 0;

  right = mem_mallocn(text->curl->len - text->curc + 1, "textline_string");
  memcpy(right, text->curl->line + text->curc, text->curl->len - text->curc + 1);

  mem_freen(text->curl->line);
  if (text->curl->format) {
    mem_freen(text->curl->format);
  }

  /* Make the new TextLine */
  ins = mem_mallocn(sizeof(TextLine), "textline");
  ins->line = left;
  ins->format = NULL;
  ins->len = text->curc;

  text->curl->line = right;
  text->curl->format = NULL;
  text->curl->len = text->curl->len - text->curc;

  lib_insertlinkbefore(&text->lines, text->curl, ins);

  text->curc = 0;

  txt_make_dirty(text);
  txt_clean_text(text);

  txt_pop_sel(text);
}


static void txt_delete_line(Text *text, TextLine *line)
{
  if (!text->curl) {
    return;
  }

  lib_remlink(&text->lines, line);

  if (line->line) {
    mem_freen(line->line);
  }
  if (line->format) {
    mem_freen(line->format);
  }

  mem_freen(line);

  txt_make_dirty(text);
  txt_clean_text(text);
}

static void txt_combine_lines(Text *text, TextLine *linea, TextLine *lineb)
{
  char *tmp, *s;

  if (!linea || !lineb) {
    return;
  }

  tmp = mem_mallocb(linea->len + lineb->len + 1, "textline_string");

  s = tmp;
  s += lib_strcpy_rlen(s, linea->line);
  s += lib_strcpy_rlen(s, lineb->line);
  (void)s;

  make_new_line(linea, tmp);

  txt_delete_line(text, lineb);

  txt_make_dirty(text);
  txt_clean_text(text);
}

void txt_duplicate_line(Text *text)
{
  TextLine *textline;

  if (!text->curl) {
    return;
  }

  if (text->curl == text->sell) {
    textline = txt_new_line(text->curl->line);
    lib_insertlinkafter(&text->lines, text->curl, textline);

    txt_make_dirty(text);
    txt_clean_text(text);
  }
}

void txt_delete_char(Text *text)
{
  unsigned int c = '\n';

  if (!text->curl) {
    return;
  }

  if (txt_has_sel(text)) { /* deleting a selection */
    txt_delete_sel(text);
    txt_make_dirty(text);
    return;
  }
  if (text->curc == text->curl->len) { /* Appending two lines */
    if (text->curl->next) {
      txt_combine_lines(text, text->curl, text->curl->next);
      txt_pop_sel(text);
    }
    else {
      return;
    }
  }
  else { /* Just deleting a char */
    size_t c_len = text->curc;
    c = lib_str_utf8_as_unicode_step(text->curl->line, text->curl->len, &c_len);
    c_len -= text->curc;
    UNUSED_VARS(c);

    memmove(text->curl->line + text->curc,
            text->curl->line + text->curc + c_len,
            text->curl->len - text->curc - c_len + 1);

    text->curl->len -= c_len;

    txt_pop_sel(text);
  }

  txt_make_dirty(text);
  txt_clean_text(text);
}

void txt_delete_word(Text *text)
{
  txt_jump_right(text, true, true);
  txt_delete_sel(text);
  txt_make_dirty(text);
}

void txt_backspace_char(Text *text)
{
  unsigned int c = '\n';

  if (!text->curl) {
    return;
  }

  if (txt_has_sel(text)) { /* deleting a selection */
    txt_delete_sel(text);
    txt_make_dirty(text);
    return;
  }
  if (text->curc == 0) { /* Appending two lines */
    if (!text->curl->prev) {
      return;
    }

    text->curl = text->curl->prev;
    text->curc = text->curl->len;

    txt_combine_lines(text, text->curl, text->curl->next);
    txt_pop_sel(text);
  }
  else { /* Just backspacing a char */
    const char *prev = lib_str_find_prev_char_utf8(text->curl->line + text->curc,
                                                   text->curl->line);
    size_t c_len = prev - text->curl->line;
    c = lib_str_utf8_as_unicode_step(text->curl->line, text->curl->len, &c_len);
    c_len -= prev - text->curl->line;

    UNUSED_VARS(c);

    /* source and destination overlap, don't use memcpy() */
    memmove(text->curl->line + text->curc - c_len,
            text->curl->line + text->curc,
            text->curl->len - text->curc + 1);

    text->curl->len -= c_len;
    text->curc -= c_len;

    txt_pop_sel(text);
  }

  txt_make_dirty(text);
  txt_clean_text(text);
}

void txt_backspace_word(Text *text)
{
  txt_jump_left(text, true, true);
  txt_delete_sel(text);
  txt_make_dirty(text);
}

/* Max spaces to replace a tab with, currently hardcoded to TXT_TABSIZE = 4.
 * Used by txt_convert_tab_to_spaces, indent and unindent.
 * Remember to change this string according to max tab size */
static char tab_to_spaces[] = "    ";

static void txt_convert_tab_to_spaces(Text *text)
{
  /* sb aims to pad adjust the tab-width needed so that the right number of spaces
   * is added so that the indentation of the line is the right width (i.e. aligned
   * to multiples of TXT_TABSIZE)  */
  const char *sb = &tab_to_spaces[text->curc % TXT_TABSIZE];
  txt_insert_buf(text, sb);
}

static bool txt_add_char_intern(Text *text, unsigned int add, bool replace_tabs)
{
  char *tmp, ch[LIB_UTF8_MAX];
  size_t add_len;

  if (!text->curl) {
    return 0;
  }

  if (add == '\n') {
    txt_split_curline(text);
    return true;
  }

  /* insert spaces rather than tabs */
  if (add == '\t' && replace_tabs) {
    txt_convert_tab_to_spaces(text);
    return true;
  }

  txt_delete_sel(text);

  add_len = lib_str_utf8_from_unicode(add, ch, sizeof(ch));

  tmp = mem_mallocn(text->curl->len + add_len + 1, "textline_string");

  memcpy(tmp, text->curl->line, text->curc);
  memcpy(tmp + text->curc, ch, add_len);
  memcpy(
      tmp + text->curc + add_len, text->curl->line + text->curc, text->curl->len - text->curc + 1);

  make_new_line(text->curl, tmp);

  text->curc += add_len;

  txt_pop_sel(text);

  txt_make_dirty(text);
  txt_clean_text(text);

  return 1;
}

bool txt_add_char(Text *text, unsigned int add)
{
  return txt_add_char_intern(text, add, (text->flags & TXT_TABSTOSPACES) != 0);
}

bool txt_add_raw_char(Text *text, unsigned int add)
{
  return txt_add_char_intern(text, add, 0);
}

void txt_delete_selected(Text *text)
{
  txt_delete_sel(text);
  txt_make_dirty(text);
}

bool txt_replace_char(Text *text, unsigned int add)
{
  unsigned int del;
  size_t del_size = 0, add_size;
  char ch[LIB_UTF8_MAX];

  if (!text->curl) {
    return false;
  }

  /* If text is selected or we're at the end of the line just use txt_add_char */
  if (text->curc == text->curl->len || txt_has_sel(text) || add == '\n') {
    return txt_add_char(text, add);
  }

  del_size = text->curc;
  del = lib_str_utf8_as_unicode_step(text->curl->line, text->curl->len, &del_size);
  del_size -= text->curc;
  UNUSED_VARS(del);
  add_size = lib_str_utf8_from_unicode(add, ch, sizeof(ch));

  if (add_size > del_size) {
    char *tmp = mem_mallocn(txt->curl->len + add_size - del_size + 1, "txtline_string");
    memcpy(tmp, txt->curl->line, txt->curc);
    memcpy(tmp + txt->curc + add_size,
           txt->curl->line + txt->curc + del_size,
           txt->curl->len - txt->curc - del_size + 1);
    mem_freen(txt->curl->line);
    txt->curl->line = tmp;
  }
  else if (add_size < del_size) {
    char *tmp = text->curl->line;
    memmove(tmp + text->curc + add_size,
            tmp + text->curc + del_size,
            txt->curl->len - txt->curc - del_size + 1);
  }

  memcpy(txt->curl->line + txt->curc, ch, add_size);
  txt->curc += add_size;
  txt->curl->len += add_size - del_size;

  txt_pop_sel(txt);
  txt_make_dirty(txt);
  txt_clean_text(txt);
  return true;
}

/* Generic prefix op, use for comment & indent.
 * caller must handle undo. */
static void txt_select_prefix(Txt *txt, const char *add, bool skip_blank_lines)
{
  int len, num, curc_old, selc_old;
  char *tmp;

  const int indentlen = strlen(add);

  lib_assert(!ELEM(NULL, txt->curl, txt->sell));

  curc_old = txt->curc;
  selc_old = txt->selc;

  num = 0;
  while (true) {

    /* don't indent blank lines */
    if ((text->curl->len != 0) || (skip_blank_lines == 0)) {
      tmp = mem_mallocn(txt->curl->len + indentlen + 1, "txtline_string");

      txt->curc = 0;
      if (txt->curc) {
        memcpy(tmp, txt->curl->line, txt->curc); /* XXX never true, check prev line */
      }
      memcpy(tmp + text->curc, add, indentlen);

      len = txt->curl->len - txt->curc;
      if (len > 0) {
        memcpy(tmp + txt->curc + indentlen, txt->curl->line + text->curc, len);
      }
      tmp[text->curl->len + indentlen] = 0;

      make_new_line(txt->curl, tmp);

      txt->curc += indentlen;

      txt_make_dirty(txt);
      txt_clean_text(txt);
    }

    if (txt->curl == txt->sell) {
      if (txt->curl->len != 0) {
        txt->selc += indentlen;
      }
      break;
    }

    txt->curl = txt->curl->next;
    num++;
  }

  while (num > 0) {
    txt->curl = txt->curl->prev;
    num--;
  }

  /* Keep the cursor left aligned if we don't have a selection. */
  if (curc_old == 0 && !(txt->curl == txt->sell && curc_old == selc_old)) {
    if (txt->curl == txt->sell) {
      if (txt->curc == txt->selc) {
        txt->selc = 0;
      }
    }
    txt->curc = 0;
  }
  else {
    if (txt->curl->len != 0) {
      txt->curc = curc_old + indentlen;
    }
  }
}

/* Generic un-prefix operation, use for comment & indent.
 *
 * param require_all: When true, all non-empty lines must have this prefix.
 * Needed for comments where we might want to un-comment a block which contains some comments.
 *
 * note caller must handle undo. */
static bool txt_select_unprefix(Txt *txt, const char *remove, const bool require_all)
{
  int num = 0;
  const int indentlen = strlen(remove);
  bool unindented_first = false;
  bool changed_any = false;

  lib_assert(!ELEM(NULL, text->curl, text->sell));

  if (require_all) {
    /* Check all non-empty lines use this 'remove',
     * so the op is applied equally or not at all. */
    TxtLine *l = txt->curl;
    while (true) {
      if (STREQLEN(l->line, remove, indentlen)) {
        /* pass */
      }
      else {
        /* Blank lines or whitespace can be skipped. */
        for (int i = 0; i < l->len; i++) {
          if (!ELEM(l->line[i], '\t', ' ')) {
            return false;
          }
        }
      }
      if (l == txt->sell) {
        break;
      }
      l = l->next;
    }
  }

  while (true) {
    bool changed = false;
    if (STREQLEN(text->curl->line, remove, indentlen)) {
      if (num == 0) {
        unindented_first = true;
      }
      text->curl->len -= indentlen;
      memmove(text->curl->line, text->curl->line + indentlen, text->curl->len + 1);
      changed = true;
      changed_any = true;
    }

    txt_make_dirty(txt);
    txt_clean_text(txt);

    if (txt->curl == txt->sell) {
      if (changed) {
        txt->selc = MAX2(txt->selc - indentlen, 0);
      }
      break;
    }

    txt->curl = txt->curl->next;
    num++;
  }

  if (unindented_first) {
    txt->curc = MAX2(txt->curc - indentlen, 0);
  }

  while (num > 0) {
    txt->curl = txt->curl->prev;
    num--;
  }

  /* caller must handle undo */
  return changed_any;
}

void txt_comment(Txt *txt)
{
  const char *prefix = "#";

  if (ELEM(NULL, txt->curl, txt->sell)) {
    return;
  }

  const bool skip_blank_lines = txt_has_sel(txt);
  txt_select_prefix(text, prefix, skip_blank_lines);
}

bool txt_uncomment(Txt *txt)
{
  const char *prefix = "#";

  if (ELEM(NULL, txt->curl, txt->sell)) {
    return false;
  }

  return txt_select_unprefix(txt, prefix, true);
}

void txt_indent(Txt *txt)
{
  const char *prefix = (txt->flags & TXT_TABSTOSPACES) ? tab_to_spaces : "\t";

  if (ELEM(NULL, txt->curl, txt->sell)) {
    return;
  }

  txt_select_prefix(txt, prefix, true);
}

bool txt_unindent(Txt *txt)
{
  const char *prefix = (txt->flags & TXT_TABSTOSPACES) ? tab_to_spaces : "\t";

  if (ELEM(NULL, text->curl, txt->sell)) {
    return false;
  }

  return txt_select_unprefix(text, prefix, false);
}

void txt_move_lines(struct Text *text, const int direction)
{
  TxtLine *line_other;

  lib_assert(ELEM(direction, TXT_MOVE_LINE_UP, TXT_MOVE_LINE_DOWN));

  if (!txt->curl || !txt->sell) {
    return;
  }

  txt_order_cursors(txt, false);

  line_other = (direction == TXT_MOVE_LINE_DOWN) ? txt->sell->next : txt->curl->prev;

  if (!line_other) {
    return;
  }

  lib_remlink(&txt->lines, line_other);

  if (direction == TXT_MOVE_LINE_DOWN) {
    lib_insertlinkbefore(&txt->lines, txt->curl, line_other);
  }
  else {
    lib_insertlinkafter(&txt->lines, txt->sell, line_other);
  }

  txt_make_dirty(txt);
  txt_clean_text(txt);
}

int txt_setcurr_tab_spaces(Txt *txt, int space)
{
  int i = 0;
  int test = 0;
  const char *word = ":";
  const char *comm = "#";
  const char indent = (text->flags & TXT_TABSTOSPACES) ? ' ' : '\t';
  static const char *back_words[] = {"return", "break", "continue", "pass", "yield", NULL};

  if (!txt->curl) {
    return 0;
  }

  while (txt->curl->line[i] == indent) {
    /* We only count those tabs/spaces that are before any text or before the curs; */
    if (i == txt->curc) {
      return i;
    }

    i++;
  }
  if (strstr(text->curl->line, word)) {
    /* if we find a ':' on this line, then add a tab but not if it is:
     * 1) in a comment
     * 2) within an id
     * 3) after the cursor (text->curc), i.e. when creating space before a function def T25414.   */
    int a;
    bool is_indent = false;
    for (a = 0; (a < text->curc) && (text->curl->line[a] != '\0'); a++) {
      char ch = text->curl->line[a];
      if (ch == '#') {
        break;
      }
      if (ch == ':') {
        is_indent = 1;
      }
      else if (!ELEM(ch, ' ', '\t')) {
        is_indent = 0;
      }
    }
    if (is_indent) {
      i += space;
    }
  }

  for (test = 0; back_words[test]; test++) {
    /* if there are these key words then remove a tab because we are done with the block */
    if (strstr(text->curl->line, back_words[test]) && i > 0) {
      if (strcspn(text->curl->line, back_words[test]) < strcspn(text->curl->line, comm)) {
        i -= space;
      }
    }
  }
  return i;
}

/* Character Queries **/
int txt_check_bracket(const char ch)
{
  int a;
  char opens[] = "([{";
  char close[] = ")]}";

  for (a = 0; a < (sizeof(opens) - 1); a++) {
    if (ch == opens[a]) {
      return a + 1;
    }
    if (ch == close[a]) {
      return -(a + 1);
    }
  }
  return 0;
}

bool txt_check_delim(const char ch)
{
  /* TODO: have a function for operators:
   * http://docs.python.org/py3k/reference/lexical_analysis.html#operators */
  int a;
  char delims[] = "():\"\' ~!%^&*-+=[]{};/<>|.#\t,@";

  for (a = 0; a < (sizeof(delims) - 1); a++) {
    if (ch == delims[a]) {
      return true;
    }
  }
  return false;
}

bool txt_check_digit(const char ch)
{
  if (ch < '0') {
    return false;
  }
  if (ch <= '9') {
    return true;
  }
  return false;
}

bool txt_check_id(const char ch)
{
  if (ch < '0') {
    return false;
  }
  if (ch <= '9') {
    return true;
  }
  if (ch < 'A') {
    return false;
  }
  if (ch <= 'Z' || ch == '_') {
    return true;
  }
  if (ch < 'a') {
    return false;
  }
  if (ch <= 'z') {
    return true;
  }
  return false;
}

bool txt_check_id_nodigit(const char ch)
{
  if (ch <= '9') {
    return false;
  }
  if (ch < 'A') {
    return false;
  }
  if (ch <= 'Z' || ch == '_') {
    return true;
  }
  if (ch < 'a') {
    return false;
  }
  if (ch <= 'z') {
    return true;
  }
  return false;
}

int txt_check_id_unicode(const unsigned int ch)
{
  return (ch < 255 && txt_check_id((unsigned int)ch));
}

int txt_check_id_nodigit_unicode(const unsigned int ch)
{
  return (ch < 255 && txt_check_id_nodigit((char)ch));
}

bool txt_check_whitespace(const char ch)
{
  if (ELEM(ch, ' ', '\t', '\r', '\n')) {
    return true;
  }
  return false;
}

int text_find_id_start(const char *str, int i)
{
  if (UNLIKELY(i <= 0)) {
    return 0;
  }

  while (i--) {
    if (!text_check_id(str[i])) {
      break;
    }
  }
  i++;
  return i;
}
