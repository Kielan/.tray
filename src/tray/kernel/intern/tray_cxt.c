#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "mem_guardedalloc.h"

#include "types_collection.h"
#include "types_pen.h"
#include "types_linestyle.h"
#include "types_object.h"
#include "types_scene.h"
#include "types_screen.h"
#include "types_space.h"
#include "types_view3d.h"
#include "types_wm.h"
#include "types_workspace.h"

#include "graph.h"

#include "lib_list.h"
#include "lib_string.h"
#include "lib_threads.h"
#include "lib_utildefines.h"

#include "lang.h"

#include "tray_cxt.h"
#include "tray_layer.h"
#include "tray_main.h"
#include "tray_scene.h"
#include "tray_screen.h"
#include "tray_sound.h"
#include "tray_workspace.h"

#include "render_engine.h"

#include "api_access.h"
#include "api_prototypes.h"

#include "CLG_log.h"

static CLG_LogRef LOG = {"tray.cxt"};

/* struct */
m
struct  xt {
  int thread;

  /* window context */
  struct {
    struct Win *win;
    struct WorkSpace *workspace;
    struct Screen *screen;
    struct ScrArea *area;
    struct ARegion *region;
    struct ARegion *menu;
    struct WinGizmoGroup *gizmo_group;
    struct CxtStore *store;

    /* Op poll. */
    /* Store the reason the poll fn fails (static string, not allocated).
     * For more advanced formatting use `op_poll_msg_dyn_params`. */
    const char *op_poll_msg;
    /* Store values to dynamically to create the string (called when a tool-tip is shown). */
    struct CxtPollMsgDyn_Params op_poll_msg_dyn_params;
  } wm;

  /* data context */
  struct {
    struct Main *main;
    struct Scene *scene;

    int recursion;
  } data;
};

/* cxt */
Cxt *cxt_create(void)
{
  Cxt *C = mem_callocn(sizeof(Cxt), "Cxt");

  return C;
}

Cxt *cxt_copy(const Cxt *C)
{
  Cxt *newC = mem_dupallocn((void *)C);

  memset(&newC->win.op_poll_msg_dyn_params, 0, sizeof(newC->win.op_poll_msg_dyn_params));

  return newC;
}

void cxt_free(Cxt *C)
{
  /* This may contain a dynamically allocated message, free. */
  cxt_win_op_poll_msg_clear(C);

  mem_freen(C);
}

/* store */
CxtStore *cxt_store_add(List *cxts, const char *name, const ApiPtr *ptr)
{
  /* ensure we have a cxt to put the entry in, if it was already used
   * we have to copy the cxt to ensure */
  CxtStore *cxt = cxts->last;

  if (!cxt || cxt->used) {
    if (ctx) {
      bContextStore *lastctx = ctx;
      ctx = MEM_dupallocN(lastctx);
      BLI_duplicatelist(&ctx->entries, &lastctx->entries);
    }
    else {
      cxt = mem_callocn(sizeof(CxtStore), "CxtStore");
    }

    lib_addtail(cxts, cxt);
  }

  CxtStoreEntry *entry = mem_callocn(sizeof(CxtStoreEntry), "CxtStoreEntry");
  lib_strncpy(entry->name, name, sizeof(entry->name));
  entry->ptr = *ptr;

  lib_addtail(&cxt->entries, entry);

  return cxt;
}

CxtStore *cxt_store_add_all(List *cxts, CxtStore *cxt)
{
  /* ensure we have a context to put the entries in, if it was already used
   * we have to copy the context to ensure */
  CxtStore *cxt = cxts->last;

  if (!cxt || cxt->used) {
    if (cxt) {
      CxtStore *lastcxt = cxt;
      cxt = mem_dupallocn(lastctx);
      lib_duplicatelist(&ctx->entries, &lastctx->entries);
    }
    else {
      ctx = mem_callocn(sizeof(CxtStore), "CxtStore");
    }

    BLI_addtail(contexts, ctx);
  }

  LISTBASE_FOREACH (bContextStoreEntry *, tentry, &context->entries) {
    bContextStoreEntry *entry = MEM_dupallocN(tentry);
    BLI_addtail(&ctx->entries, entry);
  }

  return ctx;
}

CxtStore *cxt_store_get(Cxt *C)
{
  return C->wm.store;
}

void CTX_store_set(bContext *C, bContextStore *store)
{
  C->wm.store = store;
}

bContextStore *CTX_store_copy(bContextStore *store)
{
  bContextStore *ctx = MEM_dupallocN(store);
  BLI_duplicatelist(&ctx->entries, &store->entries);

  return ctx;
}

void cxt_store_free(CxtStore *store)
{
  lib_freelistn(&store->entries);
  mem_freen(store);
}

void CTX_store_free_list(List *cxts)
{
  CxtStore *ctx;
  while ((cxt = lib_pophead(cxts))) {
    cxt_store_free(ctx);
  }
}

/* is python initialized? */

bool CTX_py_init_get(bContext *C)
{
  return C->data.py_init;
}
void CTX_py_init_set(bContext *C, bool value)
{
  C->data.py_init = value;
}

void *CTX_py_dict_get(const bContext *C)
{
  return C->data.py_context;
}
void *CTX_py_dict_get_orig(const bContext *C)
{
  return C->data.py_context_orig;
}

void CTX_py_state_push(bContext *C, struct bContext_PyState *pystate, void *value)
{
  pystate->py_context = C->data.py_context;
  pystate->py_context_orig = C->data.py_context_orig;

  C->data.py_context = value;
  C->data.py_context_orig = value;
}
void CTX_py_state_pop(bContext *C, struct bContext_PyState *pystate)
{
  C->data.py_context = pystate->py_context;
  C->data.py_context_orig = pystate->py_context_orig;
}

/* data context utility functions */

struct bContextDataResult {
  PointerRNA ptr;
  ListBase list;
  const char **dir;
  short type; /* 0: normal, 1: seq */
};

static void *ctx_wm_python_context_get(const bContext *C,
                                       const char *member,
                                       const StructRNA *member_type,
                                       void *fall_through)
{
#ifdef WITH_PYTHON
  if (UNLIKELY(C && CTX_py_dict_get(C))) {
    bContextDataResult result;
    memset(&result, 0, sizeof(bContextDataResult));
    BPY_context_member_get((bContext *)C, member, &result);

    if (result.ptr.data) {
      if (RNA_struct_is_a(result.ptr.type, member_type)) {
        return result.ptr.data;
      }

      CLOG_WARN(&LOG,
                "PyContext '%s' is a '%s', expected a '%s'",
                member,
                RNA_struct_identifier(result.ptr.type),
                RNA_struct_identifier(member_type));
    }
  }
#else
  UNUSED_VARS(C, member, member_type);
#endif

  /* don't allow UI context access from non-main threads */
  if (!BLI_thread_is_main()) {
    return NULL;
  }

  return fall_through;
}

static eContextResult ctx_data_get(bContext *C, const char *member, bContextDataResult *result)
{
  bScreen *screen;
  ScrArea *area;
  ARegion *region;
  int done = 0, recursion = C->data.recursion;
  int ret = 0;

  memset(result, 0, sizeof(bContextDataResult));
#ifdef WITH_PYTHON
  if (CTX_py_dict_get(C)) {
    if (BPY_context_member_get(C, member, result)) {
      return 1;
    }
  }
#endif

  /* don't allow UI context access from non-main threads */
  if (!BLI_thread_is_main()) {
    return done;
  }

  /* we check recursion to ensure that we do not get infinite
   * loops requesting data from ourselves in a context callback */

  /* Ok, this looks evil...
   * if (ret) done = -(-ret | -done);
   *
   * Values in order of importance
   * (0, -1, 1) - Where 1 is highest priority
   */
  if (done != 1 && recursion < 1 && C->wm.store) {
    C->data.recursion = 1;

    bContextStoreEntry *entry = BLI_rfindstring(
        &C->wm.store->entries, member, offsetof(bContextStoreEntry, name));

    if (entry) {
      result->ptr = entry->ptr;
      done = 1;
    }
  }
  if (done != 1 && recursion < 2 && (region = CTX_wm_region(C))) {
    C->data.recursion = 2;
    if (region->type && region->type->context) {
      ret = region->type->context(C, member, result);
      if (ret) {
        done = -(-ret | -done);
      }
    }
  }
  if (done != 1 && recursion < 3 && (area = CTX_wm_area(C))) {
    C->data.recursion = 3;
    if (area->type && area->type->context) {
      ret = area->type->context(C, member, result);
      if (ret) {
        done = -(-ret | -done);
      }
    }
  }

  if (done != 1 && recursion < 4 && (screen = CTX_wm_screen(C))) {
    bContextDataCallback cb = screen->context;
    C->data.recursion = 4;
    if (cb) {
      ret = cb(C, member, result);
      if (ret) {
        done = -(-ret | -done);
      }
    }
  }

  C->data.recursion = recursion;

  return done;
}

static void *ctx_data_pointer_get(const bContext *C, const char *member)
{
  bContextDataResult result;
  if (C && ctx_data_get((bContext *)C, member, &result) == CTX_RESULT_OK) {
    BLI_assert(result.type == CTX_DATA_TYPE_POINTER);
    return result.ptr.data;
  }

  return NULL;
}

static int ctx_data_pointer_verify(const bContext *C, const char *member, void **pointer)
{
  /* if context is NULL, pointer must be NULL too and that is a valid return */
  if (C == NULL) {
    *pointer = NULL;
    return 1;
  }

  bContextDataResult result;
  if (ctx_data_get((bContext *)C, member, &result) == CTX_RESULT_OK) {
    BLI_assert(result.type == CTX_DATA_TYPE_POINTER);
    *pointer = result.ptr.data;
    return 1;
  }

  *pointer = NULL;
  return 0;
}

static int ctx_data_collection_get(const bContext *C, const char *member, ListBase *list)
{
  bContextDataResult result;
  if (ctx_data_get((bContext *)C, member, &result) == CTX_RESULT_OK) {
    BLI_assert(result.type == CTX_DATA_TYPE_COLLECTION);
    *list = result.list;
    return 1;
  }

  BLI_listbase_clear(list);

  return 0;
}

static int ctx_data_base_collection_get(const bContext *C, const char *member, ListBase *list)
{
  ListBase ctx_object_list;
  if ((ctx_data_collection_get(C, member, &ctx_object_list) == false) ||
      BLI_listbase_is_empty(&ctx_object_list)) {
    BLI_listbase_clear(list);
    return 0;
  }

  bContextDataResult result;
  memset(&result, 0, sizeof(bContextDataResult));

  Scene *scene = CTX_data_scene(C);
  ViewLayer *view_layer = CTX_data_view_layer(C);

  bool ok = false;

  CollectionPointerLink *ctx_object;
  for (ctx_object = ctx_object_list.first; ctx_object; ctx_object = ctx_object->next) {
    Object *ob = ctx_object->ptr.data;
    Base *base = BKE_view_layer_base_find(view_layer, ob);
    if (base != NULL) {
      CTX_data_list_add(&result, &scene->id, &RNA_ObjectBase, base);
      ok = true;
    }
  }
  CTX_data_type_set(&result, CTX_DATA_TYPE_COLLECTION);
  BLI_freelistN(&ctx_object_list);

  *list = result.list;
  return ok;
}

PointerRNA CTX_data_pointer_get(const bContext *C, const char *member)
{
  bContextDataResult result;
  if (ctx_data_get((bContext *)C, member, &result) == CTX_RESULT_OK) {
    BLI_assert(result.type == CTX_DATA_TYPE_POINTER);
    return result.ptr;
  }

  return PointerRNA_NULL;
}

PointerRNA CTX_data_pointer_get_type(const bContext *C, const char *member, StructRNA *type)
{
  PointerRNA ptr = CTX_data_pointer_get(C, member);

  if (ptr.data) {
    if (RNA_struct_is_a(ptr.type, type)) {
      return ptr;
    }

    CLOG_WARN(&LOG,
              "member '%s' is '%s', not '%s'",
              member,
              RNA_struct_identifier(ptr.type),
              RNA_struct_identifier(type));
  }

  return PointerRNA_NULL;
}

ApiPtr cxt_data_ptr_get_type_silent(const Cxt *C, const char *member, ApiStruct *type)
{
  ApiPtr ptr = cxt_data_ptr_get(C, member);

  if (ptr.data && api_struct_is_a(ptr.type, type)) {
    return ptr;
  }

  return ApiPtrNULL;
}

List cxt_data_collection_get(const Cxt *C, const char *member)
{
  CxtDataResult result;
  if (cxt_data_get((Cxt *)C, member, &result) == CXT_RESULT_OK) {
    lib_assert(result.type == CXT_DATA_TYPE_COLLECTION);
    return result.list;
  }

  List list = {NULL, NULL};
  return list;
}

int /*eCxtResult*/ cxt_data_get(
    const Cxt *C, const char *member, ApiPtr *r_ptr, List *r_lb, short *r_type)
{
  CxtDataResult result;
  eCxtResult ret = cxt_data_get((Cxt *)C, member, &result);

  if (ret == CXT_RESULT_OK) {
    *r_ptr = result.ptr;
    *r_lb = result.list;
    *r_type = result.type;
  }
  else {
    memset(r_ptr, 0, sizeof(*r_ptr));
    memset(r_lb, 0, sizeof(*r_lb));
    *r_type = 0;
  }

  return ret;
}

static void data_dir_add(List *lb, const char *member, const bool use_all)
{
  LinkData *link;

  if ((use_all == false) && STREQ(member, "scene")) { /* exception */
    return;
  }

  if (BLI_findstring(lb, member, offsetof(LinkData, data))) {
    return;
  }

  link = MEM_callocN(sizeof(LinkData), "LinkData");
  link->data = (void *)member;
  lib_addtail(lb, link);
}

List cxt_data_dir_get_ex(const Cxt *C,
                         const bool use_store,
                         const bool use_api,
                         const bool use_all)
{
  CxtDataResult result;
  List lb;
  Screen *screen;
  ScrArea *area;
  ARgn *region;
  int a;

  memset(&lb, 0, sizeof(lb));

  if (use_api) {
    char name[256], *nameptr;
    int namelen;

    ApiProp *iterprop;
    ApiPtr cxt_ptr;
    api_ptr_create(NULL, &ApiCxt, (void *)C, &cxt_ptr);

    iterprop = api_struct_iter_prop(cxt_ptr.type);

    API_PROP_BEGIN (&cxt_ptr, itemptr, iterprop) {
      nameptr = api_struct_name_get_alloc(&itemptr, name, sizeof(name), &namelen);
      data_dir_add(&lb, name, use_all);
      if (nameptr) {
        if (name != nameptr) {
          mem_freen(nameptr);
        }
      }
    }
    API_PROP_END;
  }
  if (use_store && C->wm.store) {
    CxtStoreEntry *entry;

    for (entry = C->win.store->entries.first; entry; entry = entry->next) {
      data_dir_add(&lb, entry->name, use_all);
    }
  }
  if ((region = cxt_win_region(C)) && region->type && region->type->cxt) {
    memset(&result, 0, sizeof(result));
    region->type->cxt(C, "", &result);

    if (result.dir) {
      for (a = 0; result.dir[a]; a++) {
        data_dir_add(&lb, result.dir[a], use_all);
      }
    }
  }
  if ((area = cxt_win_area(C)) && area->type && area->type->context) {
    memset(&result, 0, sizeof(result));
    area->type->context(C, "", &result);

    if (result.dir) {
      for (a = 0; result.dir[a]; a++) {
        data_dir_add(&lb, result.dir[a], use_all);
      }
    }
  }
  if ((screen = cxt_win_screen(C)) && screen->cxt) {
    CxtDataCb cb = screen->context;
    memset(&result, 0, sizeof(result));
    cb(C, "", &result);

    if (result.dir) {
      for (a = 0; result.dir[a]; a++) {
        data_dir_add(&lb, result.dir[a], use_all);
      }
    }
  }

  return lb;
}

List cxt_data_dir_get(const Cxt *C)
{
  return cxt_data_dir_get_ex(C, true, false, false);
}

bool cxt_data_equals(const char *member, const char *str)
{
  return (STREQ(member, str));
}

bool cxt_data_dir(const char *member)
{
  return member[0] == '\0';
}

void cxt_data_id_ptr_set(CxtDataResult *result, Id *id)
{
  api_id_ptr_create(id, &result->ptr);
}

void cxt_data_ptr_set(CxtDataResult *result, Id *id, ApiStruct *type, void *data)
{
  api_ptr_create(id, type, data, &result->ptr);
}

void cxt_data_ptr_set_ptr(CxtDataResult *result, const ApiPtr *ptr)
{
  result->ptr = *ptr;
}

void cxt_data_id_list_add(CxtDataResult *result, Id *id)
{
  CollectionPtrLink *link = mem_callocn(sizeof(CollectionPtrLink), "cxt_data_id_list_add");
  api_id_ptr_create(id, &link->ptr);

  lib_addtail(&result->list, link);
}

void cxt_data_list_add(CxtDataResult *result, Id *id, ApiStruct *type, void *data)
{
  CollectionPointerLink *link = mem_callocn(sizeof(CollectionPtrLink), "cxt_data_list_add");
  api_ptr_create(id, type, data, &link->ptr);

  lib_addtail(&result->list, link);
}

void cxt_data_list_add_ptr(CxtDataResult *result, const ApiPtr *ptr)
{
  CollectionPtrLink *link = mem_callocn(sizeof(CollectionPtrLink), "cxt_data_list_add");
  link->ptr = *ptr;

  lib_addtail(&result->list, link);
}

int cxt_data_list_count(const Cxt *C, int (*fb)(const Cxt *, List *))
{
  List list;

  if (fn(C, &list)) {
    int tot = lib_list_count(&list);
    lib_freelistn(&list);
    return tot;
  }

  return 0;
}

void cxt_data_dir_set(CxtDataResult *result, const char **dir)
{
  result->dir = dir;
}

void cxt_data_type_set(CxtDataResult *result, short type)
{
  result->type = type;
}

short cxt_data_type_get(CxtDataResult *result)
{
  return result->type;
}

/* window manager cxt */
WinMngr *cxt_win_mngr(const Cxt *C)
{
  return C->win.mngr;
}

bool cxt_win_interface_locked(const Cxt *C)
{
  return (bool)C->win.mngr->is_interface_locked;
}

Win *cxt_wm(const Cxt *C)
{
  return cxt_wm_cxt_get(C, "window", &ApiWin, C->win.win);
}

WorkSpace *cxt_wm_workspace(const Cxt *C)
{
  return cxt_win_cxt_get(C, "workspace", &ApiWorkSpace, C->win.workspace);
}

Screen *cxt_win_screen(const Cxt *C)
{
  return cxt_wm_cxt_get(C, "screen", &ApiScreen, C->win.screen);
}

ScrArea *cxt_wm_area(const Cxt *C)
{
  return cxt_wm_cxt_get(C, "area", &ApiArea, C->win.area);
}

SpaceLink *cxt_win_space_data(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  return (area) ? area->spacedata.first : NULL;
}

ARegion *cxt_win_region(const Cxt *C)
{
  return cxt_win_cxt_get(C, "region", &ApiRegion, C->win.region);
}

void *cxt_wm_rgn_data(const Cxt *C)
{
  ARgn *rgn = cxt_wm_rgn(C);
  return (rgn) ? rgn->rgndata : NULL;
}

struct ARgn *cxt_win_menu(const Cxt *C)
{
  return C->win.menu;
}

struct WinGizmoGroup *cxt_win_gizmo_group(const Cxt *C)
{
  return C->win.gizmo_group;
}

struct WinMsgBus *cxt_wm_msg_bus(const Cxt *C)
{
  return C->win.mngr ? C->win.mngr->msg_bus : NULL;
}

struct ReportList *cxt_wm_reports(const Cxt *C)
{
  if (C->win.mngr) {
    return &(C->win.mngr->reports);
  }

  return NULL;
}

View3D *cxt_win_view3d(const Cxt *C)
{
  ScrArea *area = cxt_wm_area(C);
  if (area && area->spacetype == SPACE_VIEW3D) {
    return area->spacedata.first;
  }
  return NULL;
}

RgnView3D *cxt_win_rgn_view3d(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  ARgn *rgn = cxt_win_rgn(C);

  if (area && area->spacetype == SPACE_VIEW3D) {
    if (rgn && rgn->rgntype == RGN_TYPE_WIN) {
      return rgn->rgndata;
    }
  }
  return NULL;
}

struct SpaceText *cxt_win_space_text(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  if (area && area->spacetype == SPACE_TEXT) {
    return area->spacedata.first;
  }
  return NULL;
}

struct SpaceConsole *cxt_wm_space_console(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  if (area && area->spacetype == SPACE_CONSOLE) {
    return area->spacedata.first;
  }
  return NULL;
}

struct SpaceImage *cxt_wm_space_image(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  if (area && area->spacetype == SPACE_IMAGE) {
    return area->spacedata.first;
  }
  return NULL;
}

struct SpaceProps *cxt_win_space_props(const Cxt *C)
{
  ScrArea *area = cxt_wm_area(C);
  if (area && area->spacetype == SPACE_PROPS) {
    return area->spacedata.first;
  }
  return NULL;
}

struct SpaceFile *cxt_win_space_file(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  if (area && area->spacetype == SPACE_FILE) {
    return area->spacedata.first;
  }
  return NULL;
}

struct SpaceSeq *cxt_win_space_seq(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  if (area && area->spacetype == SPACE_SEQ) {
    return area->spacedata.first;
  }
  return NULL;
}

struct SpaceOutliner *cxt_win_space_outliner(const Cxt *C)
{
  ScrArea *area = cxt_wm_area(C);
  if (area && area->spacetype == SPACE_OUTLINER) {
    return area->spacedata.first;
  }
  return NULL;
}

struct SpaceNla *cxt_wm_space_nla(const Cxt *C)
{
  ScrArea *area = cxt_wm_area(C);
  if (area && area->spacetype == SPACE_NLA) {
    return area->spacedata.first;
  }
  return NULL;
}

struct SpaceNode *cxt_wm_space_node(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  if (area && area->spacetype == SPACE_NODE) {
    return area->spacedata.first;
  }
  return NULL;
}

struct SpaceGraph *cxt_win_space_graph(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  if (area && area->spacetype == SPACE_GRAPH) {
    return area->spacedata.first;
  }
  return NULL;
}

struct SpaceAction *cxt_win_space_action(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  if (area && area->spacetype == SPACE_ACTION) {
    return area->spacedata.first;
  }
  return NULL;
}

struct SpaceInfo *cxt_wm_space_info(const Cxt *C)
{
  ScrArea *area = cxt_wm_area(C);
  if (area && area->spacetype == SPACE_INFO) {
    return area->spacedata.first;
  }
  return NULL;
}

struct SpaceUserPref *cxt_win_space_userpref(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  if (area && area->spacetype == SPACE_USERPREF) {
    return area->spacedata.first;
  }
  return NULL;
}

struct SpaceClip *cxt_win_space_clip(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  if (area && area->spacetype == SPACE_CLIP) {
    return area->spacedata.first;
  }
  return NULL;
}

struct SpaceTopBar *cxt_win_space_topbar(const Cxt *C)
{
  ScrArea *area = cxt_wm_area(C);
  if (area && area->spacetype == SPACE_TOPBAR) {
    return area->spacedata.first;
  }
  return NULL;
}

struct SpaceSpreadsheet *cxt_wm_space_spreadsheet(const Cxt *C)
{
  ScrArea *area = cxt_win_area(C);
  if (area && area->spacetype == SPACE_SPREADSHEET) {
    return area->spacedata.first;
  }
  return NULL;
}

void cxt_wim_manager_set(Cxt *C, Win *win)
{
  C->win.manager = win;
  C->win.win = NULL;
  C->win.screen = NULL;
  C->win.area = NULL;
  C->win.region = NULL;
}

void cxt_win_set(Cxt *C, Win *win)
{
  C->win.window = win;
  if (win) {
    C->data.scene = win->scene;
  }
  C->win.workspace = (win) ? workspace_active_get(win->workspace_hook) : NULL;
  C->win.screen = (win) ? workspace_active_screen_get(win->workspace_hook) : NULL;
  C->win.area = NULL;
  C->win.region = NULL;
}

void cxt_win_screen_set(Cxt *C, Screen *screen)
{
  C->win.screen = screen;
  C->win.area = NULL;
  C->win.region = NULL;
}

void cxt_win_area_set(Cxt *C, ScrArea *area)
{
  C->win.area = area;
  C->win.region = NULL;
}

void cxt_win_region_set(Cxt *C, ARegion *region)
{
  C->win.region = region;
}

void cxt_wm_menu_set(Cxt *C, ARegion *menu)
{
  C->win.menu = menu;
}

void cxt_win_gizmo_group_set(Cxt *C, struct WinGizmoGroup *gzgroup)
{
  C->win.gizmo_group = gzgroup;
}

void cxt_wm_op_poll_msg_clear(Cxt *C)
{
  struct CxtPollMsgDyn_Params *params = &C->win.op_poll_msg_dyn_params;
  if (params->free_fn != NULL) {
    params->free_fn(C, params->user_data);
  }
  params->get_fn = NULL;
  params->free_fn = NULL;
  params->user_data = NULL;

  C->win.op_poll_msg = NULL;
}
void cxt_wm_op_poll_msg_set(Cxt *C, const char *msg)
{
  cxt_win_op_poll_msg_clear(C);

  C->win.op_poll_msg = msg;
}

void cxt_wm_op_poll_msg_set_dynamic(Cxt *C, const struct CxtPollMsgDyn_Params *params)
{
  cxt_win_op_poll_msg_clear(C);

  C->win.op_poll_msg_dyn_params = *params;
}

const char *cxt_win_op_poll_msg_get(Cxt *C, bool *r_free)
{
  struct CxtPollMsgDyn_Params *params = &C->win.op_poll_msg_dyn_params;
  if (params->get_fn != NULL) {
    char *msg = params->get_fn(C, params->user_data);
    if (msg != NULL) {
      *r_free = true;
    }
    return msg;
  }

  *r_free = false;
  return IFACE_(C->wm.op_poll_msg);
}

/* data cxt */
Main *cxt_data_main(const Cxt *C)
{
  Main *main;
  if (cxt_data_ptr_verify(C, "tray_data", (void *)&main)) {
    return bmain;
  }

  return C->data.main;
}

void cxt_data_main_set(Cxt *C, Main *main)
{
  C->data.main = main;
  sound_init_main(main);
}

Scene *cxt_data_scene(const Cxt *C)
{
  Scene *scene;
  if (ctx_data_ptr_verify(C, "scene", (void *)&scene)) {
    return scene;
  }

  return C->data.scene;
}

ViewLayer *cxt_data_view_layer(const Cxt *C)
{
  ViewLayer *view_layer;

  if (cxt_data_ptr_verify(C, "view_layer", (void *)&view_layer)) {
    return view_layer;
  }

  Win *win = cxt_win(C);
  Scene *scene = cxt_data_scene(C);
  if (win) {
    view_layer = BKE_view_layer_find(scene, win->view_layer_name);
    if (view_layer) {
      return view_layer;
    }
  }

  return view_layer_default_view(scene);
}

RenderEngineType *cxt_data_engine_type(const Cxt *C)
{
  Scene *scene = cxt_data_scene(C);
  return render_engines_find(scene->r.engine);
}

LayerCollection *cxt_data_layer_collection(const Cxt *C)
{
  ViewLayer *view_layer = cxt_data_view_layer(C);
  LayerCollection *layer_collection;

  if (cxt_data_pointer_verify(C, "layer_collection", (void *)&layer_collection)) {
    if (view_layer_has_collection(view_layer, layer_collection->collection)) {
      return layer_collection;
    }
  }

  /* fallback */
  return layer_collection_get_active(view_layer);
}

Collection *cxt_data_collection(const Cxt *C)
{
  Collection *collection;
  if (cxt_data_ptr_verify(C, "collection", (void *)&collection)) {
    return collection;
  }

  LayerCollection *layer_collection = cxt_data_layer_collection(C);
  if (layer_collection) {
    return layer_collection->collection;
  }

  /* fallback */
  Scene *scene = cxt_data_scene(C);
  return scene->master_collection;
}

enum eCxtObjectMode cxt_data_mode_enum_ex(const Object *obedit,
                                          const Object *ob,
                                          const eObjectMode object_mode)
{
  // Object *obedit = CTX_data_edit_object(C);
  if (obedit) {
    switch (obedit->type) {
      case OB_FONT:
        return CTX_MODE_EDIT_TEXT;
    }
  }
  else {
    // Object *ob = CTX_data_active_object(C);
    if (ob) {
      if (object_mode & OB_MODE_PARTICLE_EDIT) {
        return CTX_MODE_PARTICLE;
      }
    }
  }

  return CXT_MODE_OBJECT;
}

enum eCxtObjectMode cxt_data_mode_enum(const Cxt *C)
{
  Object *obedit = cxt_data_edit_object(C);
  Object *obact = obedit ? NULL : cxt_data_active_object(C);
  return cxt_data_mode_enum_ex(obedit, obact, obact ? obact->mode : OB_MODE_OBJECT);
}

/* Would prefer if we can use the enum version below over this one?
 * note Must be aligned with above enum. */
static const char *data_mode_strings[] = {
    "text_edit",
    "particlemode",
    "objectmode",
    NULL,
};
LIB_STATIC_ASSERT(ARRAY_SIZE(data_mode_strings) == CXT_MODE_NUM + 1,
                  "Must have a string for each cxt mode")
const char *cxt_data_mode_string(const Cxt *C)
{
  return data_mode_strings[cxt_data_mode_enum(C)];
}

void cxt_data_scene_set(Cxt *C, Scene *scene)
{
  C->data.scene = scene;
}

ToolSettings *cxt_data_tool_settings(const Cxt *C)
{
  Scene *scene = cxt_data_scene(C);

  if (scene) {
    return scene->toolsettings;
  }

  return NULL;
}

int cxt_data_selected_ids(const Cxt *C, List *list)
{
  return cxt_data_collection_get(C, "selected_ids", list);
}

int cxt_data_selected_nodes(const Cxt *C, List *list)
{
  return cxt_data_collection_get(C, "selected_nodes", list);
}

int cxt_data_selectable_objects(const Cxt *C, List *list)
{
  return cxt_data_collection_get(C, "selectable_objects", list);
}

int cxt_data_selectable_bases(const Cxt *C, List *list)
{
  return cxt_data_base_collection_get(C, "selectable_objects", list);
}

struct Object *cxt_data_active_object(const Cxt *C)
{
  return cxt_data_ptr_get(C, "active_object");
}

struct Base *cxt_data_active_base(const Cxt *C)
{
  Object *ob = cxt_data_ptr_get(C, "active_object");

  if (ob == NULL) {
    return NULL;
  }

  ViewLayer *view_layer = cxt_data_view_layer(C);
  return view_layer_base_find(view_layer, ob);
}

struct Object *cxt_data_edit_object(const Cxt *C)
{
  return cxt_data_ptr_get(C, "edit_object");
}

struct Image *cxt_data_edit_image(const Cxt *C)
{
  return cxt_data_ptr_get(C, "edit_image");
}

struct Text *cxt_data_edit_text(const Cxt *C)
{
  return cxt_data_ptr_get(C, "edit_text");
}

struct MovieClip *cxt_data_edit_movieclip(const Cxt *C)
{
  return cxt_data_ptr_get(C, "edit_movieclip");
}

struct CacheFile *cxt_data_edit_cachefile(const Cxt *C)
{
  return cxt_data_ptr_get(C, "edit_cachefile");
}

const AssetLibRef *cxt_wm_asset_lib_ref(const Cxt *C)
{
  return cxt_data_ptr_get(C, "asset_lib_ref");
}

AssetHandle cxt_wm_asset_handle(const Cxt *C, bool *r_is_valid)
{
  AssetHandle *asset_handle_p =
      (AssetHandle *)cxt_data_ptr_get_type(C, "asset_handle", &ApiAssetHandle).data;
  if (asset_handle_p) {
    *r_is_valid = true;
    return *asset_handle_p;
  }

  /* If the asset handle was not found in context directly, try if there's an active file with
   * asset data there instead. Not nice to have this here, would be better to have this in
   * `ED_asset.h`, but we can't include that in BKE. Even better would be not needing this at all
   * and being able to have editors return this in the usual `context` callback. But that would
   * require returning a non-owning pointer, which we don't have in the Asset Browser (yet). */
  FileDirEntry *file =
      (FileDirEntry *)CTX_data_pointer_get_type(C, "active_file", &RNA_FileSelectEntry).data;
  if (file && file->asset_data) {
    *r_is_valid = true;
    return (AssetHandle){.file_data = file};
  }

  *r_is_valid = false;
  return (AssetHandle){0};
}

Graph *cxt_data_graph_ptr(const Cxt *C)
{
  Main *bmain = cxt_data_main(C);
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  graph *graph = scene_ensure_graph(main, scene, view_layer);
  /* Dependency graph might have been just allocated, and hence it will not be marked.
   * This confuses redo system due to the lack of flushing changes back to the original data.
   * In the future we would need to check whether the CTX_wm_window(C)  is in editing mode (as an
   * opposite of playback-preview-only) and set active flag based on that. */
  graph_make_active(graph);
  return graph;
}

Graph *cxt_data_expect_evald_graph(const Cxt *C)
{
  Graph *graph = cxt_data_graph_ptr(C);
  /* TODO: Assert that the dependency graph is fully evaluated.
   * Note that first the depsgraph and scene post-eval hooks needs to run extra round of updates
   * first to make check here really reliable. */
  return graph;
}

Graph *cxt_data_ensure_evald_graph(const Cxt *C)
{
  Graph *graph = cxt_data_graph_ptr(C);
  Main *main = cxt_data_main(C);
  scene_graph_eval_ensure(graph, main);
  return graph;
}

Graph *cxt_data_graph_on_load(const Cxt *C)
{
  Scene *scene = cxt_data_scene(C);
  ViewLayer *view_layer = cxt_data_view_layer(C);
  return scene_get_graph(scene, view_layer);
}
