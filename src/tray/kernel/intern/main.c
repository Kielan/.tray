/* Contains management of #Main database itself. */

#include <string.h>

#include "mem_guardedalloc.h"

#include "lib_dunelib.h"
#include "lib_ghash.h"
#include "lib_mempool.h"
#include "lib_threads.h"

#include "STRUCT_ID.h"

#include "dune_global.h"
#include "dune_idtype.h"
#include "dune_lib_id.h"
#include "dune_lib_query.h"
#include "dune_main.h"
#include "dune_main_idmap.h"

#include "imbuf.h"
#include "imbuf_types.h"

Main *dune_main_new(void)
{
  Main *main = mem_callocn(sizeof(Main), "new main");
  main->lock = mem_mallocn(sizeof(SpinLock), "main lock");
  lib_spin_init((SpinLock *)bmain->lock);
  return main;
}

void dune_main_free(Main *mainvar)
{
  /* also call when reading a file, erase all, etc */
  List *lbarray[INDEX_ID_MAX];
  int a;

  /* Since we are removing whole main, no need to bother 'properly'
   * (and slowly) removing each ID from it. */
  const int free_flag = (LIB_ID_FREE_NO_MAIN | LIB_ID_FREE_NO_UI_USER |
                         LIB_ID_FREE_NO_USER_REFCOUNT | LIB_ID_FREE_NO_DEG_TAG);

  MEM_SAFE_FREE(mainvar->dune_thumb);

  a = set_listpyrs(mainvar, lbarray);
  while (a--) {
    List *lb = lbarray[a];
    Id *id, *id_next;

    for (id = lb->first; id != NULL; id = id_next) {
      id_next = id->next;
#if 1
      dune_id_free_ex(mainvar, id, free_flag, false);
#else
      /* errors freeing ID's can be hard to track down,
       * enable this so valgrind will give the line number in its error log */
      switch (a) {
        case 0:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 1:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 2:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 3:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 4:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 5:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 6:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 7:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 8:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 9:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 10:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 11:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 12:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 13:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 14:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 15:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 16:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 17:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 18:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 19:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 20:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 21:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 22:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 23:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 24:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 25:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 26:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 27:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 28:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 29:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 30:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 31:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 32:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 33:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        case 34:
          dune_id_free_ex(mainvar, id, free_flag, false);
          break;
        default:
          lib_assert_unreachable();
          break;
      }
#endif
    }
    lib_list_clear(lb);
  }

  if (mainvar->relations) {
    dune_main_relations_free(mainvar);
  }

  if (mainvar->id_map) {
    dune_main_idmap_destroy(mainvar->id_map);
  }

  lib_spin_end((SpinLock *)mainvar->lock);
  mem_freen(mainvar->lock);
  mem_freen(mainvar);
}

bool dune_main_is_empty(struct Main *main)
{
  Id *id_iter;
  FOREACH_MAIN_ID_BEGIN (main, id_iter) {
    return false;
  }
  FOREACH_MAIN_ID_END;
  return true;
}

void dune_main_lock(struct Main *main)
{
  lib_spin_lock((SpinLock *)main->lock);
}

void dune_main_unlock(struct Main *main)
{
  lib_spin_unlock((SpinLock *)main->lock);
}

static int main_relations_create_idlink_cb(LibIdLinkCbData *cb_data)
{
  MainIdRelations *main_relations = cb_data->user_data;
  Id *id_self = cb_data->id_self;
  Id **id_ptr = cb_data->id_ptr;
  const int cb_flag = cb_data->cb_flag;

  if (*id_ptr) {
    MainIdRelationsEntry **entry_p;

    /* Add `id_ptr` as child of `id_self`. */
    {
      if (!lib_ghash_ensure_p(
              main_relations->relations_from_ptrs, id_self, (void ***)&entry_p)) {
        *entry_p = mem_callocn(sizeof(**entry_p), __func__);
        (*entry_p)->session_uuid = id_self->session_uuid;
      }
      else {
        lib_assert((*entry_p)->session_uuid == id_self->session_uuid);
      }
      MainIdRelationsEntryItem *to_id_entry = lib_mempool_alloc(main_relations->entry_items_pool);
      to_id_entry->next = (*entry_p)->to_ids;
      to_id_entry->id_ptr.to = id_ptr;
      to_id_entry->session_uuid = (*id_ptr != NULL) ? (*id_ptr)->session_uuid :
                                                          MAIN_ID_SESSION_UUID_UNSET;
      to_id_entry->usage_flag = cb_flag;
      (*entry_p)->to_ids = to_id_entry;
    }

    /* Add `id_self` as parent of `id_ptr`. */
    if (*id_ptr != NULL) {
      if (!lib_ghash_ensure_p(
              main_relations->relations_from_ptrs, *id_ptr, (void ***)&entry_p)) {
        *entry_p = mem_callocn(sizeof(**entry_p), __func__);
        (*entry_p)->session_uuid = (*id_ptr)->session_uuid;
      }
      else {
        lib_assert((*entry_p)->session_uuid == (*id_ptr)->session_uuid);
      }
      MainIdRelationsEntryItem *from_id_entry = lib_mempool_alloc(
          main_relations->entry_items_pool);
      from_id_entry->next = (*entry_p)->from_ids;
      from_id_entry->id_ptr.from = id_self;
      from_id_entry->session_uuid = id_self->session_uuid;
      from_id_entry->usage_flag = cb_flag;
      (*entry_p)->from_ids = from_id_entry;
    }
  }

  return IDWALK_RET_NOP;
}

void dune_main_relations_create(Main *main, const short flag)
{
  if (main->relations != NULL) {
    dune_main_relations_free(main);
  }

  main->relations = mem_mallocn(sizeof(*main->relations), __func__);
  main->relations->relations_from_ptrs = lib_ghash_new(
      lib_ghashutil_ptrhash, lib_ghashutil_ptrcmp, __func__);
  main->relations->entry_items_pool = lib_mempool_create(
      sizeof(MainIdRelationsEntryItem), 128, 128, LIB_MEMPOOL_NOP);

  main->relations->flag = flag;

  Id *id;
  FOREACH_MAIN_ID_BEGIN (main, id) {
    const int idwalk_flag = IDWALK_READONLY |
                            ((flag & MAINIDRELATIONS_INCLUDE_UI) != 0 ? IDWALK_INCLUDE_UI : 0);

    /* Ensure all Ids do have an entry, even if they are not connected to any other. */
    MainIdRelationsEntry **entry_p;
    if (!lib_ghash_ensure_p(main->relations->relations_from_ptrs, id, (void ***)&entry_p)) {
      *entry_p = mem_callocn(sizeof(**entry_p), __func__);
      (*entry_p)->session_uuid = id->session_uuid;
    }
    else {
      lib_assert((*entry_p)->session_uuid == id->session_uuid);
    }

    dune_lib_foreach_id_link(
        NULL, id, main_relations_create_idlink_cb, main->relations, idwalk_flag);
  }
  FOREACH_MAIN_ID_END;
}

void dune_main_relations_free(Main *main)
{
  if (main->relations != NULL) {
    if (main->relations->relations_from_ptrs != NULL) {
      lib_ghash_free(main->relations->relations_from_ptrs, NULL, mem_freen);
    }
    lib_mempool_destroy(main->relations->entry_items_pool);
    mem_freen(main->relations);
    bmain->relations = NULL;
  }
}

void dune_main_relations_tag_set(struct Main *main,
                                 const eMainIdRelationsEntryTags tag,
                                 const bool value)
{
  if (main->relations == NULL) {
    return;
  }

  GHashIter *gh_iter;
  for (gh_iter = lib_ghashIterator_new(main->relations->relations_from_ptrs);
       !lib_ghashIter_done(gh_iter));
       lib_ghashIter_step(gh_iter)) {
    MainIdRelationsEntry *entry = lib_ghashIter_getValue(gh_iter);
    if (value) {
      entry->tags |= tag;
    }
    else {
      entry->tags &= ~tag;
    }
  }
  lib_ghashIter_free(gh_iter);
}

GSet *dune_main_gset_create(Main *main, GSet *gset)
{
  if (gset == NULL) {
    gset = lib_gset_new(lib_ghashutil_ptrhash, lib_ghashutil_ptrcmp, __func__);
  }

  Id *id;
  FOREACH_MAIN_ID_BEGIN (main, id) {
    lib_gset_add(gset, id);
  }
  FOREACH_MAIN_ID_END;
  return gset;
}

/* Utils for ID's library weak reference API. */
typedef struct LibWeakRefKey {
  char filepath[FILE_MAX];
  char id_name[MAX_ID_NAME];
} LibWeakRefKey;

static LibWeakRefKey *lib_weak_key_create(LibWeakRefKey *key,
                                          const char *lib_path,
                                          const char *id_name)
{
  if (key == NULL) {
    key = mem_mallocn(sizeof(*key), __func__);
  }
  lib_strncpy(key->filepath, lib_path, sizeof(key->filepath));
  lib_strncpy(key->id_name, id_name, sizeof(key->id_name));
  return key;
}

static uint lib_weak_key_hash(const void *ptr)
{
  const LibWeakRefKey *string_pair = ptr;
  uint hash = lib_ghashutil_strhash_p_murmur(string_pair->filepath);
  return hash ^ lib_ghashutil_strhash_p_murmur(string_pair->id_name);
}

static bool lib_weak_key_cmp(const void *a, const void *b)
{
  const LibWeakRefKey *string_pair_a = a;
  const LibWeakRefKey *string_pair_b = b;

  return !(STREQ(string_pair_a->filepath, string_pair_b->filepath) &&
           STREQ(string_pair_a->id_name, string_pair_b->id_name));
}

GHash *dune_main_lib_weak_refer_create(Main *main)
{
  GHash *lib_weak_ref_mapping = lib_ghash_new(
      lib_weak_key_hash, lib_weak_key_cmp, __func__);

  List *lb;
  FOREACH_MAIN_LIST_BEGIN (main, lb) {
    Id *id_iter = lb->first;
    if (id_iter == NULL) {
      continue;
    }
    if (!dune_idtype_idcode_append_is_reusable(GS(id_iter->name))) {
      continue;
    }
    lib_assert(dune_idtype_idcode_is_linkable(GS(id_iter->name)));

    FOREACH_MAIN_LIST_ID_BEGIN (lb, id_iter) {
      if (id_iter->lib_weak_ref == NULL) {
        continue;
      }
      LibWeakRefKey *key = lib_weak_key_create(NULL,
                                               id_iter->lib_weak_ref->lib_filepath,
                                               id_iter->lib_weak_ref->lib_id_name);
      lib_ghash_insert(lib_weak_ref_mapping, key, id_iter);
    }
    FOREACH_MAIN_LIST_ID_END;
  }
  FOREACH_MAIN_LIST_END;

  return lib_weak_ref_mapping;
}

void dune_main_lib_weak_ref_destroy(GHash *lib_weak_ref_mapping)
{
  lib_ghash_free(lib_weak_ref_mapping, mem_freen, NULL);
}

Id *dune_main_lib_weak_ref_search_item(GHash *lib_weak_ref_mapping,
                                       const char *lib_filepath,
                                       const char *lib_id_name)
{
  LibWeakRefKey key;
  lib_weak_key_create(&key, lib_filepath, lib_id_name);
  return (Id *)lib_ghash_lookup(lib_weak_ref_mapping, &key);
}

void dune_main_lib_weak_ref_add_item(GHash *library_weak_ref_mapping,
                                     const char *lib_filepath,
                                     const char *lib_id_name,
                                     Id *new_id)
{
  lib_assert(GS(lib_id_name) == GS(new_id->name));
  lib_assert(new_id->lib_weak_ref == NULL);
  lib_assert(dune_idtype_idcode_append_is_reusable(GS(new_id->name)));

  new_id->lib_weak_ref = mem_mallocn(sizeof(*(new_id->lib_weak_ref)),
                                               __func__);

  LibWeakRefKey *key = lib_weak_key_create(NULL, lib_filepath, lib_id_name);
  void **id_p;
  const bool already_exist_in_mapping = lib_ghash_ensure_p(
      lib_weak_ref_mapping, key, &id_p);
  lib_assert(!already_exist_in_mapping);
  UNUSED_VARS_NDEBUG(already_exist_in_mapping);

  lib_strncpy(new_id->lib_weak_ref->lib_filepath,
              lib_filepath,
              sizeof(new_id->lib_weak_ref->lib_filepath));
  lib_strncpy(new_id->lib_weak_ref->lib_id_name,
              lib_id_name,
              sizeof(new_id->lib_weak_ref->lib_id_name));
  *id_p = new_id;
}

void dune_main_lib_weak_ref_update_item(GHash *lib_weak_ref_mapping,
                                        const char *lib_filepath,
                                        const char *lib_id_name,
                                        Id *old_id,
                                        Id *new_id)
{
  lib_assert(GS(lib_id_name) == GS(old_id->name));
  lib_assert(GS(lib_id_name) == GS(new_id->name));
  lib_assert(old_id->lib_weak_ref != NULL);
  lib_assert(new_id->lib_weak_ref == NULL);
  lib_assert(STREQ(old_id->lib_weak_ref->lib_filepath, lib_filepath));
  lib_assert(STREQ(old_id->lib_weak_ref->lib_id_name, lib_id_name));

  LibWeakRefKey key;
  lib_weak_key_create(&key, lib_filepath, lib_id_name);
  void **id_p = lib_ghash_lookup_p(lib_weak_ref_mapping, &key);
  lib_assert(id_p != NULL && *id_p == old_id);

  new_id->lib_weak_ref = old_id->lib_weak_ref;
  old_id->lib_weak_ref = NULL;
  *id_p = new_id;
}

void dune_main_lib_weak_ref_remove_item(GHash *lib_weak_ref_mapping,
                                        const char *lib_filepath,
                                        const char *lib_id_name,
                                        Id *old_id)
{
  lib_assert(GS(lib_id_name) == GS(old_id->name));
  lib_assert(old_id->lib_weak_ref != NULL);

  LibWeakRefKey key;
  lib_weak_key_create(&key, lib_filepath, lib_id_name);

  lib_assert(lib_ghash_lookup(lib_weak_ref_mapping, &key) == old_id);
  lib_ghash_remove(lib_weak_ref_mapping, &key, mem_freen, NULL);

  MEM_SAFE_FREE(old_id->library_weak_ref);
}

DuneThumbnail *dune_main_thumbnail_from_imbuf(Main *main, ImBuf *img)
{
  DuneThumbnail *data = NULL;

  if (bmain) {
    MEM_SAFE_FREE(bmain->blen_thumb);
  }

  if (img) {
    const size_t sz = DUNE_THUMB_MEMSIZE(img->x, img->y);
    data = MEM_mallocN(sz, __func__);

    imbuf_rect_from_float(img); /* Just in case... */
    data->width = img->x;
    data->height = img->y;
    memcpy(data->rect, img->rect, sz - sizeof(*data));
  }

  if (bmain) {
    main->blen_thumb = data;
  }
  return data;
}

ImBuf *dune_main_thumbnail_to_imbuf(Main *bmain, DuneThumbnail *data)
{
  ImBuf *img = NULL;

  if (!data && main) {
    data = main->dune_thumb;
  }

  if (data) {
    img = IMB_allocFromBuffer(
        (const uint *)data->rect, NULL, (uint)data->width, (uint)data->height, 4);
  }

  return img;
}

void dune_main_thumbnail_create(struct Main *main)
{
  MEM_SAFE_FREE(bmain->dune_thumb);

  main->dune_thumb = mem_callocn(DUNE_THUMB_MEMSIZE(DUNE_THUMB_SIZE, DUNE_THUMB_SIZE), __func__);
  main->dune_thumb->width = DUNE_THUMB_SIZE;
  main->dune_thumb->height = DUNE_THUMB_SIZE;
}

const char *dune_main_dunefile_path(const Main *bmain)
{
  return bmain->filepath;
}

const char *dune_main_dunefile_path_from_global(void)
{
  return dune_main_file_path(G_MAIN);
}

List *which_libbase(Main *main, short type)
{
  switch ((ID_Type)type) {
    case ID_SCE:
      return &(main->scenes);
    case ID_LI:
      return &(main->libraries);
    case ID_OB:
      return &(main->objects);
    case ID_ME:
      return &(main->meshes);
    case ID_CU_LEGACY:
      return &(main->curves);
    case ID_MB:
      return &(main->metaballs);
    case ID_MA:
      return &(main->materials);
    case ID_TE:
      return &(main->textures);
    case ID_IM:
      return &(main->images);
    case ID_LT:
      return &(main->lattices);
    case ID_LA:
      return &(main->lights);
    case ID_CA:
      return &(main->cameras);
    case ID_IP:
      return &(main->ipo);
    case ID_KE:
      return &(main->shapekeys);
    case ID_WO:
      return &(main->worlds);
    case ID_SCR:
      return &(main->screens);
    case ID_VF:
      return &(main->fonts);
    case ID_TXT:
      return &(main->texts);
    case ID_SPK:
      return &(main->speakers);
    case ID_LP:
      return &(main->lightprobes);
    case ID_SO:
      return &(main->sounds);
    case ID_GR:
      return &(main->collections);
    case ID_AR:
      return &(main->armatures);
    case ID_AC:
      return &(main->actions);
    case ID_NT:
      return &(main->nodetrees);
    case ID_BR:
      return &(main->brushes);
    case ID_PA:
      return &(main->particles);
    case ID_WM:
      return &(main->wm);
    case ID_GD:
      return &(main->pens);
    case ID_MC:
      return &(main->movieclips);
    case ID_MSK:
      return &(main->masks);
    case ID_LS:
      return &(main->linestyles);
    case ID_PAL:
      return &(main->palettes);
    case ID_PC:
      return &(main->paintcurves);
    case ID_CF:
      return &(main->cachefiles);
    case ID_WS:
      return &(main->workspaces);
    case ID_CV:
      return &(main->hair_curves);
    case ID_PT:
      return &(main->pointclouds);
    case ID_VO:
      return &(main->volumes);
    case ID_SIM:
      return &(main->simulations);
  }
  return NULL;
}

int set_listbasepointers(Main *main, List *lb[/*INDEX_ID_MAX*/])
{
  /* Libraries may be accessed from pretty much any other ID. */
  lb[INDEX_ID_LI] = &(main->lib);

  lb[INDEX_ID_IP] = &(main->ipo);

  /* Moved here to avoid problems when freeing with animato (aligorith). */
  lb[INDEX_ID_AC] = &(main->actions);

  lb[INDEX_ID_KE] = &(main->shapekeys);

  /* Referenced by gpencil, so needs to be before that to avoid crashes. */
  lb[INDEX_ID_PAL] = &(main->palettes);

  /* Referenced by nodes, objects, view, scene etc, before to free after. */
  lb[INDEX_ID_GD] = &(main->pens);

  lb[INDEX_ID_NT] = &(main->nodetrees);
  lb[INDEX_ID_IM] = &(main->images);
  lb[INDEX_ID_TE] = &(main->textures);
  lb[INDEX_ID_MA] = &(main->materials);
  lb[INDEX_ID_VF] = &(main->fonts);

  /* Important!: When adding a new object type,
   * the specific data should be inserted here. */

  lb[INDEX_ID_AR] = &(main->armatures);

  lb[INDEX_ID_CF] = &(main->cachefiles);
  lb[INDEX_ID_ME] = &(main->meshes);
  lb[INDEX_ID_CU_LEGACY] = &(main->curves);
  lb[INDEX_ID_MB] = &(main->metaballs);
  lb[INDEX_ID_CV] = &(main->hair_curves);
  lb[INDEX_ID_PT] = &(main->pointclouds);
  lb[INDEX_ID_VO] = &(main->volumes);

  lb[INDEX_ID_LT] = &(main->lattices);
  lb[INDEX_ID_LA] = &(main->lights);
  lb[INDEX_ID_CA] = &(main->cameras);

  lb[INDEX_ID_TXT] = &(main->texts);
  lb[INDEX_ID_SO] = &(main->sounds);
  lb[INDEX_ID_GR] = &(main->collections);
  lb[INDEX_ID_PAL] = &(main->palettes);
  lb[INDEX_ID_PC] = &(main->paintcurves);
  lb[INDEX_ID_BR] = &(main->brushes);
  lb[INDEX_ID_PA] = &(main->particles);
  lb[INDEX_ID_SPK] = &(main->speakers);
  lb[INDEX_ID_LP] = &(main->lightprobes);

  lb[INDEX_ID_WO] = &(main->worlds);
  lb[INDEX_ID_MC] = &(main->movieclips);
  lb[INDEX_ID_SCR] = &(main->screens);
  lb[INDEX_ID_OB] = &(main->objects);
  lb[INDEX_ID_LS] = &(main->linestyles); /* referenced by scenes */
  lb[INDEX_ID_SCE] = &(main->scenes);
  lb[INDEX_ID_WS] = &(main->workspaces); /* before wm, so it's freed after it! */
  lb[INDEX_ID_WM] = &(main->wm);
  lb[INDEX_ID_MSK] = &(main->masks);
  lb[INDEX_ID_SIM] = &(main->simulations);

  lb[INDEX_ID_NULL] = NULL;

  return (INDEX_ID_MAX - 1);
}
