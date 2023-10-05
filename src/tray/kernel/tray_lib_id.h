#pragma once

/* API to manage data-blocks inside of Blender's Main data-base, or as independent runtime-only
 * data.
 *
 * note `lib_` files are for operations over data-blocks themselves, although they might
 * alter Main as well (when creating/renaming/deleting an Id e.g.).
 *
 * section Fn Names
 *
 * warning Descriptions below is ideal goal, current status of naming does not yet fully follow it
 * (this is WIP).
 *
 * - `id_` should be used for rather high-level ops, that involve Main database and
 *   relations with other Ids, and can be considered as 'safe' (as in, in themselves, they leave
 *   affected Ids/Main in a consistent status).
 * - `libblock_` should be used for lower level ops, that perform some parts of
 *   `id_` ones, but will generally not ensure caller that affected data is in a consisted
 *   state by their own ex alone.
 * - `lib_main_` should be used for ops performed over all Ids of a given Main
 *   data-base.
 *
 * note External code should typically not use `libblock_` fns, except in some
 * specific cases requiring advanced (and potentially dangerous) handling. */

#include "lib_compiler_attrs.h"
#include "lib_utildefines.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Writer;
struct GHash;
struct Id;
struct Lib;
struct List;
struct Main;
struct ApiPtr;
struct ApiProp;
struct Cxt;

/* Get allocation size of a given data-block type and optionally allocation name. */
size_t libblock_get_alloc_info(short type, const char **name);
/* Allocates and returns memory of the right size for the specified block type,
 * initialized to zero */
void *libblock_alloc_notest(short type) ATTR_WARN_UNUSED_RESULT;
/* Allocates and returns a block of the specified type, with the specified name
 * (adjusted as necessary to ensure uniqueness), and appended to the specified list.
 * The user count is set to 1, all other content (apart from name and links) being
 * initialized to zero. */
void *libblock_alloc(struct Main *main, short type, const char *name, int flag)
    ATTR_WARN_UNUSED_RESULT;
/* Initialize an Id of given type, such that it has valid 'empty' data.
 * Id is assumed to be just calloc'ed */
void libblock_init_empty(struct Id *id) ATTR_NONNULL(1);

/* Reset the runtime counters used by Id remapping. */
void libblock_runtime_reset_remapping_status(struct Id *id) ATTR_NONNULL(1);

/* Id's session_uuid management */
/* When an Id's uuid is of that value, it is unset/invalid (e.g. for runtime Ids, etc.) */
#define MAIN_ID_SESSION_UUID_UNSET 0

/* Generate a session-wise uuid for the given id.
 * note "session-wise" here means while editing a given .dune file. Once a new .dune file is
 * loaded or created, undo history is cleared/reset, and so is the uuid counter. */
void libblock_session_uuid_ensure(struct Id *id);
/* Re-generate a new session-wise uuid for the given id.
 *
 * warning This has a few very specific use-cases, no other usage is expected currently:
 *   - To handle UI-related data-blocks that are kept across new file reading, when we do keep
 * existing UI.
 *   - For Ids that are made local without needing any copying. */
void libblock_session_uuid_renew(struct Id *id);

/* Generic helper to create a new empty data-block of given type in given main database.
 * param name: can be NULL, in which case we get default name for this Id type. */
void *id_new(struct Main *main, short type, const char *name);
/* Generic helper to create a new temporary empty data-block of given type,
 * *outside* of any Main database.
 * param name: can be NULL, in which case we get default name for this Id type. */
void *id_new_nomain(short type, const char *name);

/* New Id creation/copying options */
enum {
  /* Generic options (should be handled by all Id types copying, ID creation, etc.). *** */
  /* Create datablock outside of any main database -
   * similar to 'localize' functions of materials etc. */
  ID_CREATE_NO_MAIN = 1 << 0,
  /* Do not affect user refcount of datablocks used by new one
   * (which also gets zero usercount then).
   * Implies ID_CREATE_NO_MAIN. */
  ID_CREATE_NO_USER_REFCOUNT = 1 << 1,
  /* Assume given 'newid' already points to allocated memory for whole datablock
   * (Id + data) - USE WITH CAUTION!
   * Implies ID_CREATE_NO_MAIN. */
  ID_CREATE_NO_ALLOCATE = 1 << 2,

  /* Do not tag new Id for update in graph. */
  ID_CREATE_NO_GRAPH_TAG = 1 << 8,

  /* Very similar to LIB_ID_CREATE_NO_MAIN, and should never be used with it (typically combined
   * with ID_CREATE_LOCALIZE or LIB_ID_COPY_LOCALIZE in fact).
   * It ensures that IDs created with it will get the LIB_TAG_LOCALIZED tag, and uses some
   * specific code in some copy cases (mostly for node trees). */
  ID_CREATE_LOCAL = 1 << 9,

  /* Create for the graph, when set LIB_TAG_COPIED_ON_WRITE must be set.
   * Internally this is used to share some ptrs instead of duplicating them. */
  ID_COPY_SET_COPIED_ON_WRITE = 1 << 10,

  /* Specific options to some Id types or usages */
  /* Copy runtime data caches. */
  ID_COPY_CACHES = 1 << 18,
  /* Mesh: Ref CD data layers instead of doing real copy - USE WITH CAUTION! */
  ID_COPY_CD_REF = 1 << 20,
  /* Do not copy id->override_library, used by Id data-block override routines. */
  ID_COPY_NO_LIB_OVERRIDE = 1 << 21,
  /* When copying local sub-data (like constraints or modifiers), do not set their "library
   * override local data" flag. */
  ID_COPY_NO_LIB_OVERRIDE_LOCAL_DATA_FLAG = 1 << 22,

  /* Keep the lib ptr when copying data-block outside of bmain. */
  ID_COPY_KEEP_LIB = 1 << 25,
  /* EXCEPTION! Specific deep-copy of node trees used e.g. for rendering purposes. */
  ID_COPY_NODETREE_LOCALIZE = 1 << 27,
  /* EXCEPTION! Specific handling of RB objects regarding collections differs depending whether we
   * duplicate scene/collections, or objects. */
  ID_COPY_RIGID_BODY_NO_COLLECTION_HANDLING = 1 << 28,

  /* Create a local, outside of main, data-block to work on. */
  ID_CREATE_LOCALIZE = ID_CREATE_NO_MAIN | ID_CREATE_NO_USER_REFCOUNT |
                           ID_CREATE_NO_GRAPH_TAG,
  /* Generate a local copy, outside of main, to work on (used by COW e.g.). */
  ID_COPY_LOCALIZE = ID_CREATE_LOCALIZE | ID_COPY_NO_PREVIEW | ID_COPY_CACHES |
                         ID_COPY_NO_LIB_OVERRIDE,
};

void libblock_copy_ex(struct Main *main,
                      const struct Id *id,
                      struct Id **r_newid,
                      int orig_flag);
/* Used everywhere in dune kernel  */
void *libblock_copy(struct Main *main, const struct Id *id) ATTR_WARN_UNUSED_RESULT
    ATTR_NONNULL();

/* Sets the name of a block to name, suitably adjusted for uniqueness. */
void libblock_rename(struct Main *main, struct Id *id, const char *name) ATTR_NONNULL();
/* Use after setting the Id's name
 * When name exists: call 'new_id' */
void libblock_ensure_unique_name(struct Main *main, const char *name) ATTR_NONNULL();

struct Id *libblock_find_name(struct Main *main,
                              short type,
                              const char *name) ATTR_WARN_UNUSED_RESULT ATTR_NONNULL();
struct Id *libblock_find_session_uuid(struct Main *main, short type, uint32_t session_uuid);
/* Duplicate (a.k.a. deep copy) common processing options.
 * See also eDupFlags for options controlling what kind of Ids to duplicate. */
typedef enum eIdFlagsDup {
  /* This call to a duplicate function is part of another call for some parent Id.
   * Therefore, this sub-process should not clear `newid` ptrs, nor handle remapping itself.
   * NOTE: In some cases (like Object one), the duplicate fn may be called on the root Id
   * with this flag set, as remapping and/or other similar tasks need to be handled by the caller. */
  ID_DUP_IS_SUBPROCESS = 1 << 0,
  /* This call is performed on a 'root' Id, and should therefore perform some decisions regarding
   * sub-IDs (dependencies), check for linked vs. locale data, etc. */
  ID_DUP_IS_ROOT_ID = 1 << 1,
} eIdFlagsDup;

ENUM_OPS(eIdFlagsDup, ID_DUP_IS_ROOT_ID)

/* lib_remap.c (keep here since they're general functions) */
/* New freeing logic options. */
enum {
  /* Generic options (should be handled by all ID types freeing). *** */
  /* Do not try to remove freed ID from given Main (passed Main may be NULL). */
  ID_FREE_NO_MAIN = 1 << 0,
  /* Do not affect user refcount of datablocks used by freed one.
   * Implies LIB_ID_FREE_NO_MAIN. */
  ID_FREE_NO_USER_REFCOUNT = 1 << 1,
  /* Assume freed Id datablock memory is managed elsewhere, do not free it
   * (still calls relevant ID type's freeing function though) - USE WITH CAUTION!
   * Implies LIB_ID_FREE_NO_MAIN */
  ID_FREE_NOT_ALLOCATED = 1 << 2,

  /* Do not tag freed ID for update in graph. */
  ID_FREE_NO_DEG_TAG = 1 << 8,
  /* Do not attempt to remove freed ID from UI data/notifiers/... */
  ID_FREE_NO_UI_USER = 1 << 9,
};

void libblock_free_datablock(struct Id *id, int flag) ATTR_NONNULL();
void libblock_free_data(struct Id *id, bool do_id_user) ATTR_NONNULL();

/* In most cases dune_id_free_ex handles this, when lower level fns are called directly
 * this fn will need to be called too, if Python has access to the data.
 *
 * Id data-blocks such as Material.nodetree are not stored in Main. */
void libblock_free_data_py(struct Id *id);

/* Complete Id freeing, extended version for corner cases.
 * Can override default (and safe!) freeing process, to gain some speed up.
 *
 * At that point, given id is assumed to not be used by any other data-block already
 * (might not be actually true, in case e.g. several inter-related Ids get freed together...).
 * However, they might still be using (referencing) other Ids, this code takes care of it if
 * LIB_TAG_NO_USER_REFCOUNT is not defined.
 *
 * param main: Main database containing the freed Id,
 * can be NULL in case it's a temp ID outside of any Main.
 * param idv: Ptr to ID to be freed.
 * param flag: Set of a LIB_ID_FREE_... flags controlling/overriding usual freeing process,
 * 0 to get default safe behavior.
 * param use_flag_from_idtag: Still use freeing info flags from given Id datablock,
 * even if some overriding ones are passed in flag param. */
void id_free_ex(struct Main *main, void *idv, int flag, bool use_flag_from_idtag);
/* Complete Id freeing, should be usable in most cases (even for out-of-Main Ids).
 *
 * See id_free_ex description for full details.
 *
 * param main: Main database containing the freed Id,
 * can be NULL in case it's a temp Id outside of any Main.
 * param idv: Pointer to Id to be freed  */
void id_free(struct Main *main, void *idv);

/* Not really a freeing function by itself,
 * it decrements usercount of given id, and only frees it if it reaches 0. */
void id_free_us(struct Main *main, void *idv) ATTR_NONNULL();

/* Properly delete a single Id from given main database. */
void id_delete(struct Main *main, void *idv) ATTR_NONNULL();
/* Properly delete all Ids tagged with LIB_TAG_DOIT, in given \a bmain database.
 *
 * This is more efficient than calling dune_id_delete repetitively on a large set of IDs
 * (several times faster when deleting most of the Ids at once).
 *
 * warning Considered experimental for now, seems to be working OK but this is
 * risky code in a complicated area.
 * return Number of deleted datablocks. */
size_t id_multi_tagged_delete(struct Main *main) ATTR_NONNULL();

/* Add a 'NO_MAIN' data-block to given main (also sets usercounts of its Ids if needed). */
void libblock_management_main_add(struct Main *main, void *idv);
/* Remove a data-block from given main (set it to 'NO_MAIN' status). */
void libblock_management_main_remove(struct Main *main, void *idv);

void libblock_management_usercounts_set(struct Main *main, void *idv);
void libblock_management_usercounts_clear(struct Main *main, void *idv);

void id_lib_extern(struct Id *id);
void id_lib_indirect_weak_link(struct Id *id);
/* Ensure we have a real user
 *
 * note Now that we have flags, we could get rid of the 'fake_user' special case,
 * flags are enough to ensure we always have a real user.
 * However, ID_REAL_USERS is used in several places outside of core lib.c,
 * so think we can wait later to make this change. */
void id_us_ensure_real(struct Id *id);
void id_us_clear_real(struct Id *id);
/* Same as id_us_plus, but does not handle lib indirect -> extern.
 * Only used by readfile.c so far, but simpler/safer to keep it here nonetheless. */
void id_us_plus_no_lib(struct Id *id);
void id_us_plus(struct Id *id);
/* decrements the user count for *id. */
void id_us_min(struct Id *id);
void id_fake_user_set(struct Id *id);
void id_fake_user_clear(struct Id *id);
void id_newptr_and_tag_clear(struct Id *id);

/* Flags to control make local code behavior. */
enum {
  /* Making that Id local is part of making local a whole library. */
  ID_MAKELOCAL_FULL_LIB = 1 << 0,

  /* In case caller code already knows this ID should be made local without copying. */
  ID_MAKELOCAL_FORCE_LOCAL = 1 << 1,
  /* In case caller code already knows this ID should be made local using copying. */
  ID_MAKELOCAL_FORCE_COPY = 1 << 2,

  /* Clear asset data (in case the ID can actually be made local, in copy case asset data is never
   * copied over). */
  ID_MAKELOCAL_ASSET_DATA_CLEAR = 1 << 3,
};

/* Helper to decide whether given `id` can be directly made local, or needs to be copied.
 * `r_force_local` and `r_force_copy` cannot be true together. But both can be false, in case no
 * action should be performed.
 *
 * note low-level helper to de-duplicate logic between `id_make_local_generic` and the
 * specific corner-cases implementations needed for objects and brushes. */
void id_make_local_generic_action_define(
    struct Main *main, struct Id *id, int flags, bool *r_force_local, bool *r_force_copy);
/* Generic 'make local' function, works for most of data-block types. */
void id_make_local_generic(struct Main *main, struct Id *id, int flags);
/* Calls the appropriate make_local method for the block, unless test is set.
 *
 * note Always set id.newid ptr in case it gets duplicated.
 *
 * param flags: Special flag used when making a whole library's content local,
 * it needs specific handling.
 * return true is the Id has successfully been made local. */
bool id_make_local(struct Main *main, struct Id *id, int flags);
/* note Does *not* set id.newid ptr. */
bool id_single_user(struct Cxt *C,
                    struct Id *id,
                    struct ApiPtr *ptr,
                    struct ApiProp *prop);
bool id_copy_is_allowed(const struct Id *id);
/* Invokes the appropriate copy method for the block and returns the result in
 * Id.newid, unless test. Returns true if the block can be copied. */
struct Id *id_copy(struct Main *main, const struct Id *id);
/* Generic entry point for copying a data-block (new API).
 *
 * note Copy is generally only affecting the given data-block
 * (no Id used by copied one will be affected, besides user-count).
 *
 * There are exceptions though:
 * - Embedded Ids (root node trees and master collections) are always copied with their owner.
 * - If ID_COPY_ACTIONS is defined, actions used by anim-data will be duplicated.
 * - If ID_COPY_SHAPEKEY is defined, shape-keys will be duplicated.
 * - If ID_CREATE_LOCAL is defined, root node trees will be deep-duplicated recursively.
 *
 * note User-count of new copy is always set to 1.
 *
 * param main: Main database, may be NULL only if LIB_ID_CREATE_NO_MAIN is specified.
 * param id: Source data-block.
 * param r_newid: Ptr to new (copied) Id ptr, may be NULL.
 * Used to allow copying into already allocated memory.
 * param flag: Set of copy options, see `types_id.h` enum for details
 * (leave to zero for default, full copy).
 * return NULL when copying that Id type is not supported, the new copy otherwise. */
struct Id *id_copy_ex(struct Main *main, const struct Id *id, struct Id **r_newid, int flag);
/* Invokes the appropriate copy method for the block and returns the result in
 * newid, unless test. Returns true if the block can be copied. */
struct Id *id_copy_for_duplicate(struct Main *main,
                                 struct Id *id,
                                 uint duplicate_flags,
                                 int copy_flags);

/* Does a mere memory swap over the whole Ids data (including type-specific memory).
 * note Most internal Id data itself is not swapped (only IdProps are).
 *
 * param main: May be NULL, in which case there will be no remapping of internal ptrs to
 * itself. */
void id_swap(struct Main *main, struct Id *id_a, struct Id *id_b);
/* Does a mere memory swap over the whole Ids data (including type-specific memory).
 * note All internal Id data itself is also swapped.
 *
 * param main: May be NULL, in which case there will be no remapping of internal ptrs to
 * itself. */
void id_swap_full(struct Main *main, struct Id *id_a, struct Id *id_b);

/* Sort given id into given list list, using case-insensitive comparison of the id names.
 *
 * note All other Ids beside given one are assumed already properly sorted in the list.
 *
 * param id_sorting_hint: Ignored if NULL. Otherwise, used to check if we can insert id
 * immediately before or after that ptr. It must always be into given list list. */
void id_sort_by_name(struct List *list, struct Id *id, struct Id *id_sorting_hint);
/* Expand Id usages of given id as 'extern' (and no more indirect) linked data.
 * Used by Id copy/make_local fns. */
void id_expand_local(struct Main *main, struct Id *id, int flags);

/* Ensures given Id has a unique name in given list.
 *
 * Only for local Ids (linked ones already have a unique Id in their library).
 *
 * param do_linked_data: if true, also ensure a unique name in case the given \a id is linked
 * (otherwise, just ensure that it is properly sorted).
 *
 * return true if a new name had to be created. */
bool id_new_name_validate(struct List *list,
                          struct Id *id,
                          const char *name,
                          bool do_linked_data) ATTR_NONNULL(1, 2);
/* Pull an Id out of a lib (make it local). Only call this for IDs that
 * don't have other lib users.
 *
 * param flags: Same set of `LIB_ID_MAKELOCAL_` flags as passed to lib_id_make_local. */
void id_clear_lib_data(struct Main *main, struct Id *id, int flags);

/* Clear or set given tags for all ids of given type in `main` (runtime tags).
 *
 * note Affect whole Main database. */
void id_tag_idcode(struct Main *mainvar, short type, int tag, bool value);
/* Clear or set given tags for all ids in list (runtime tags). */
void id_tag_list(struct List *list, int tag, bool value);
/* Clear or set given tags for all ids in main (runtime tags). */
void id_tag_all(struct Main *mainvar, int tag, bool value);

/* Clear or set given flags for all ids in list (persistent flags). */
void id_flag_list(struct List *list, int flag, bool value);
/* Clear or set given flags for all ids in main (persistent flags). */
void id_flag_all(struct Main *main, int flag, bool value);

/* Next to indirect usage in `readfile.c/writefile.c` also in `editobject.c`, `scene.c`. */
void id_newptr_and_tag_clear(struct Main *main);

void id_refcount_recompute(struct Main *main, bool do_linked_only);

void lib_objects_recalc_all(struct Main *main);

/* Only for repairing files via versioning, avoid for general use. */
void id_repair_duplicate_names_list(struct List *list);

#define MAX_ID_FULL_NAME (64 + 64 + 3 + 1)         /* 64 is MAX_ID_NAME - 2 */
#define MAX_ID_FULL_NAME_UI (MAX_ID_FULL_NAME + 3) /* Adds 'keycode' two letters at beginning. */
/* Generate full name of the data-block (without Id code, but with lib if any).
 *
 * note Result is unique to a given ID type in a given Main database.
 *
 * param name: An allocated string of minimal length MAX_ID_FULL_NAME,
 * will be filled with generated string.
 * param sep_char: Char to use for separating name and lib name.
 * Can be 0 to use default (' '). */
void id_full_name_get(char name[MAX_ID_FULL_NAME], const struct Id *id, char separator_char);
/* Generate full name of the data-block (without Id code, but with lib if any),
 * with a 2 to 3 character prefix prepended indicating whether it comes from a lib,
 * is overriding, has a fake or no user, etc.
 *
 * note Result is unique to a given Id type in a given Main database.
 *
 * param name: An allocated string of minimal length MAX_ID_FULL_NAME_UI,
 * will be filled with generated string.
 * param sep_char: Character to use for separating name and lib name.
 * Can be 0 to use default (' ').
 * param r_prefix_len: The length of the prefix added. */
void id_full_name_ui_prefix_get(char name[MAX_ID_FULL_NAME_UI],
                                const struct Id *id,
                                bool add_lib_hint,
                                char separator_char,
                                int *r_prefix_len);

/* Generate a concatenation of Id name (including two-chars type code) and its lib name, if any.
 *
 * return A unique allocated string key for any Id in the whole Main database.*/
