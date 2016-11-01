#define EFL_BETA_API_SUPPORT
#define EFL_EO_API_SUPPORT
#include <Eo.h>
#include <Eet.h>
#include <Ecore.h>
#include <Elementary.h>
#include <Emotion.h>

#include "common.h"

#define FILESEP "file://"
#define FILESEP_LEN sizeof(FILESEP) - 1

static Eet_Data_Descriptor *_music_edd = NULL;

typedef enum
{
   MEDIA_LIBRARY = 1,
   MEDIA_PLAYLIST = 2,
   MEDIA_URI = 3,
   MEDIA_JANGO = 4
} Media_Type;

/*
 * Library:
 *    Name given by the user
 *    Path local path
 *    List of dynamic URI elements
 * Playlist:
 *    Name given by the user
 *    Path - none
 *    List of static URI elements
 * URI:
 *    Name: filename
 *    Path: absolute path
 *    Dynamic list for library
 *    No static list
 */
typedef struct _Media_Element Media_Element;

struct _Media_Element
{
   Media_Type type;
   const char *name;
   const char *path;
   Eina_List *static_elts;
   /* Not in eet */
   Ecore_File_Monitor *monitor;
   Media_Element *parent;
   Eina_List *dynamic_elts;
   Jango_Session *jango;
   Elm_Genlist_Item *gl_item;
   Eina_Bool expanded : 1;
   Eina_Bool playing : 1;
};

typedef struct
{
   Eina_List *elements; /* List of Media_Element */
} Music_Cfg;

static Music_Cfg *_cfg = NULL;

static Eo *_win = NULL, *_media_gl = NULL, *_popup = NULL;
static Eina_Stringshare *_cfg_filename = NULL;
static Elm_Genlist_Item_Class *_media_itc = NULL;

static Eo *_ply_emo = NULL, *_play_total_lb = NULL, *_play_prg_lb = NULL, *_play_prg_sl = NULL;
static Eo *_play_bt = NULL, *_play_song_lb = NULL;

static Media_Element *_playing_media = NULL, *_main_playing_media = NULL;

static Eo *_selected_gl = NULL;

static void
_eet_load()
{
   Eet_Data_Descriptor_Class eddc;
   if (!_music_edd)
     {
        /* Media Element */
        EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Media_Element);
        Eet_Data_Descriptor *media_element_edd = eet_data_descriptor_stream_new(&eddc);
        EET_DATA_DESCRIPTOR_ADD_BASIC(media_element_edd, Media_Element, "name", name, EET_T_STRING);
        EET_DATA_DESCRIPTOR_ADD_BASIC(media_element_edd, Media_Element, "path", path, EET_T_STRING);
        EET_DATA_DESCRIPTOR_ADD_BASIC(media_element_edd, Media_Element, "type", type, EET_T_UINT);
        EET_DATA_DESCRIPTOR_ADD_LIST(media_element_edd, Media_Element, "static_elts", static_elts, media_element_edd);

        /* Music Cfg */
        EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Music_Cfg);
        _music_edd = eet_data_descriptor_stream_new(&eddc);
        EET_DATA_DESCRIPTOR_ADD_LIST(_music_edd, Music_Cfg, "elements", elements, media_element_edd);
     }
}

static void
_write_to_file(const char *filename)
{
   Eet_File *file = eet_open(filename, EET_FILE_MODE_WRITE);
   eet_data_write(file, _music_edd, "entry", _cfg, EINA_TRUE);
   eet_close(file);
}

static char *
_media_text_get(void *data, Evas_Object *obj EINA_UNUSED, const char *part EINA_UNUSED)
{
   Media_Element *melt = data;
   return strdup(melt->name?melt->name:melt->path);
}

static void
_media_element_del(Media_Element *melt)
{
   Eina_List *itr;
   Media_Element *selt;
   EINA_LIST_FOREACH(melt->dynamic_elts, itr, selt) _media_element_del(selt);
   EINA_LIST_FOREACH(melt->static_elts, itr, selt) _media_element_del(selt);

   eina_stringshare_del(melt->name);
   eina_stringshare_del(melt->path);
   ecore_file_monitor_del(melt->monitor);
   elm_object_item_del(melt->gl_item);
   if (melt->parent)
     {
        melt->parent->dynamic_elts = eina_list_remove(melt->parent->dynamic_elts, melt);
        melt->parent->static_elts = eina_list_remove(melt->parent->static_elts, melt);
     }
   // jango_session_del(melt->jango);
   free(melt);
}

static void
_media_glitem_create(Media_Element *melt)
{
   if (!melt || melt->gl_item) return;
   Media_Element *parent = melt->parent;
   Eina_List *found_itr = NULL;

   if (!_media_itc)
     {
        _media_itc = elm_genlist_item_class_new();
        _media_itc->item_style = "default";
        _media_itc->func.text_get = _media_text_get;
     }
   if (parent)
     {
        found_itr = eina_list_data_find_list(parent->static_elts, melt);
        if (!found_itr) found_itr = eina_list_data_find_list(parent->dynamic_elts, melt);
     }
   else
     {
        found_itr = eina_list_data_find_list(_cfg->elements, melt);
     }
   Media_Element *prev_elt = eina_list_data_get(eina_list_prev(found_itr));
   if (prev_elt)
     {
        melt->gl_item = elm_genlist_item_insert_after(_media_gl, _media_itc, melt, parent ? parent->gl_item : NULL,
              prev_elt->gl_item,
              melt->static_elts || melt->dynamic_elts?ELM_GENLIST_ITEM_TREE:ELM_GENLIST_ITEM_NONE,
              NULL, NULL);
     }
   else
     {
        melt->gl_item = elm_genlist_item_prepend(_media_gl, _media_itc, melt, parent ? parent->gl_item : NULL,
              melt->static_elts || melt->dynamic_elts?ELM_GENLIST_ITEM_TREE:ELM_GENLIST_ITEM_NONE,
              NULL, NULL);
     }
   efl_weak_ref(&(melt->gl_item));
   elm_genlist_item_expanded_set(melt->gl_item, melt->expanded);
}

static void
_media_glitem_refresh(Media_Element *melt)
{
   Eina_List *itr;
   Media_Element *selt;
   if (!melt) return;
   if (!_media_gl) return;
   if (!melt->gl_item)
     {
        _media_glitem_create(melt);
        return;
     }
   Elm_Genlist_Item_Type expected_type = melt->static_elts || melt->dynamic_elts?ELM_GENLIST_ITEM_TREE:ELM_GENLIST_ITEM_NONE;
   if (elm_genlist_item_type_get(melt->gl_item) != expected_type)
     {
        elm_object_item_del(melt->gl_item);
        _media_glitem_create(melt);
        return;
     }
   EINA_LIST_FOREACH(melt->static_elts, itr, selt)
     {
        _media_glitem_refresh(selt);
     }
   EINA_LIST_FOREACH(melt->dynamic_elts, itr, selt)
     {
        _media_glitem_refresh(selt);
     }
}

static void
_media_genlist_refresh()
{
   Eina_List *itr;
   Media_Element *melt;
   if (!_media_gl) return;
   EINA_LIST_FOREACH(_cfg->elements, itr, melt)
     {
        _media_glitem_refresh(melt);
     }
}

static void
_expand(void *data EINA_UNUSED, Evas_Object *cont EINA_UNUSED, void *event_info)
{
   Elm_Object_Item *glit = event_info;
   Media_Element *melt = elm_object_item_data_get(glit) ,*selt;
   Eina_List *itr;
   melt->expanded = EINA_TRUE;
   EINA_LIST_FOREACH(melt->static_elts, itr, selt)
     {
        _media_glitem_create(selt);
     }
   EINA_LIST_FOREACH(melt->dynamic_elts, itr, selt)
     {
        _media_glitem_create(selt);
     }
}

static void
_contract(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Elm_Object_Item *glit = event_info;
   Media_Element *melt = elm_object_item_data_get(glit);
   elm_genlist_item_subitems_clear(glit);
   melt->expanded = EINA_FALSE;
}

static void
_expand_req(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Elm_Object_Item *glit = event_info;
   elm_genlist_item_expanded_set(glit, EINA_TRUE);
}

static void
_contract_req(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Elm_Object_Item *glit = event_info;
   elm_genlist_item_expanded_set(glit, EINA_FALSE);
}

static void
_media_list_clear(Media_Element *melt)
{
   Media_Element *selt;
   EINA_LIST_FREE(melt->static_elts, selt)
     {
        _media_list_clear(selt);
        free(selt);
     }
   EINA_LIST_FREE(melt->dynamic_elts, selt)
     {
        _media_list_clear(selt);
        free(selt);
     }
}

static void
_media_length_update(void *data, const Efl_Event *ev)
{
   double val = emotion_object_play_length_get(data ? data : ev->object);
   char str[16];
   sprintf(str, "%.2d:%.2d:%.2d", ((int)val) / 3600, (((int)val) % 3600) / 60, ((int)val) % 60);
   if (_play_total_lb) elm_object_text_set(_play_total_lb, str);
   if (_play_prg_sl) elm_slider_min_max_set(_play_prg_sl, 0, val);
}

static void
_media_position_update(void *data, const Efl_Event *ev)
{
   double val = emotion_object_position_get(data ? data : ev->object);
   char str[16];
   sprintf(str, "%.2d:%.2d:%.2d", ((int)val) / 3600, (((int)val) % 3600) / 60, ((int)val) % 60);
   if (_play_prg_lb) elm_object_text_set(_play_prg_lb, str);
   if (_play_prg_sl) elm_slider_value_set(_play_prg_sl, val);
}

static Media_Element *
_media_find_next(Media_Element *current)
{
   if (current->dynamic_elts) return eina_list_data_get(current->dynamic_elts);
   if (current->static_elts) return eina_list_data_get(current->static_elts);
   while (current)
     {
        Media_Element *parent, *next;
        Eina_List *cur_list;
        if (current == _main_playing_media) return NULL;
        parent = current->parent;
        if (!parent) return current; // ???
        cur_list = eina_list_data_find_list(parent->dynamic_elts, current);
        if (!cur_list) cur_list = eina_list_data_find_list(parent->static_elts, current);
        if (cur_list)
          {
             next = eina_list_data_get(eina_list_next(cur_list));
             if (next) return next;
          }
        current = parent;
     }
   return NULL;
}

static void
_media_play_set(Media_Element *elt, Eina_Bool play)
{
   if (!elt) return;
   elt->playing = play;
   if (!play || elt != _playing_media)
     {
        /* Pause || the selected path is different of the played path */
        if (!play)
          {
             elm_object_part_content_set(_play_bt, "icon",
                   icon_create(_play_bt, "media-playback-start", NULL));
          }
        if (elt != _playing_media) elm_object_text_set(_play_song_lb, "");
        if (_playing_media) _playing_media->playing = EINA_FALSE;
        emotion_object_play_set(_ply_emo, EINA_FALSE);
     }
   if (play)
     {
        elm_object_part_content_set(_play_bt, "icon",
              icon_create(_play_bt, "media-playback-pause", NULL));
        if (elt == _playing_media)
          {
             /* Play again when finished - int conversion is needed
              * because the returned values are not exactly the same. */
             if ((int)emotion_object_position_get(_ply_emo) == (int)emotion_object_play_length_get(_ply_emo))
                emotion_object_position_set(_ply_emo, 0);
          }
        else
          {
             _playing_media = elt;
             emotion_object_file_set(_ply_emo, _playing_media->path);
             elm_object_text_set(_play_song_lb, _playing_media->name);
          }
        emotion_object_play_set(_ply_emo, EINA_TRUE);
     }
}

static void
_media_finished(void *data EINA_UNUSED, const Efl_Event *ev EINA_UNUSED)
{
   _media_play_set(_playing_media, EINA_FALSE);
   Media_Element *next = _media_find_next(_playing_media);
   if (next) _media_play_set(next, EINA_TRUE);
   else _main_playing_media = NULL;
}

static void
_media_tree_sanitize(Media_Element *melt)
{
   Eina_List *itr, *itr2;
   Media_Element *selt;
   EINA_LIST_FOREACH_SAFE(melt->dynamic_elts, itr, itr2, selt)
     {
        if (!ecore_file_exists(selt->path)) _media_element_del(selt);
        else if (ecore_file_is_dir(selt->path)) _media_tree_sanitize(selt);
     }
   EINA_LIST_FOREACH_SAFE(melt->static_elts, itr, itr2, selt)
     {
        if (!ecore_file_exists(selt->path)) _media_element_del(selt);
        else if (ecore_file_is_dir(selt->path)) _media_tree_sanitize(selt);
     }
}

static Media_Element *
_find_media_element_by_name(Eina_List *lst, const char *name)
{
   Eina_List *itr;
   Media_Element *melt;
   name = eina_stringshare_add(name);
   EINA_LIST_FOREACH(lst, itr, melt)
     {
        if (name == melt->name) goto end;
     }
   melt = NULL;
end:
   eina_stringshare_del(name);
   return melt;
}

static void
_dir_update(void *data, Ecore_File_Monitor *em EINA_UNUSED, Ecore_File_Event event EINA_UNUSED, const char *path EINA_UNUSED)
{
   Media_Element *melt = data;
   if (!melt) return;
   switch (melt->type)
     {
      case MEDIA_LIBRARY: case MEDIA_URI:
           {
              Eina_List *files_lst = ecore_file_ls(melt->path);
              Eina_List *old_media_list = melt->dynamic_elts;
              Media_Element *selt;
              char *name;
              melt->dynamic_elts = NULL;
              EINA_LIST_FREE(files_lst, name)
                {
                   selt = _find_media_element_by_name(old_media_list, name);
                   if (selt)
                     {
                        old_media_list = eina_list_remove(old_media_list, selt);
                     }
                   else
                     {
                        char full_path[1024];
                        selt = calloc(1, sizeof(*selt));
                        selt->type = MEDIA_URI;
                        selt->parent = melt;
                        sprintf(full_path, "%s/%s", melt->path, name);
                        selt->path = eina_stringshare_add(full_path);
                        selt->name = eina_stringshare_add(name);
                     }
                   melt->dynamic_elts = eina_list_append(melt->dynamic_elts, selt);
                   if (ecore_file_is_dir(selt->path))
                     {
                        if (!selt->monitor) selt->monitor = ecore_file_monitor_add(selt->path, _dir_update, selt);
                        _dir_update(selt, NULL, ECORE_FILE_EVENT_NONE, NULL);
                     }
                   free(name);
                }
              EINA_LIST_FREE(old_media_list, selt) _media_element_del(selt);
              break;
           }
      case MEDIA_JANGO:
           {
              if (!melt->jango || !melt->jango->download_dir) break;
              Eina_List *files_lst = ecore_file_ls(melt->jango->download_dir);
              Eina_List *old_media_list = melt->dynamic_elts;
              Media_Element *selt;
              char *name;
              melt->dynamic_elts = NULL;
              EINA_LIST_FREE(files_lst, name)
                {
                   selt = _find_media_element_by_name(old_media_list, name);
                   if (selt)
                     {
                        old_media_list = eina_list_remove(old_media_list, selt);
                     }
                   else
                     {
                        char full_path[1024];
                        selt = calloc(1, sizeof(*selt));
                        selt->type = MEDIA_URI;
                        selt->parent = melt;
                        sprintf(full_path, "%s/%s", melt->path, name);
                        selt->path = eina_stringshare_add(full_path);
                        selt->name = eina_stringshare_add(name);
                     }
                   melt->dynamic_elts = eina_list_append(melt->dynamic_elts, selt);
                   free(name);
                }
              EINA_LIST_FREE(old_media_list, selt) _media_element_del(selt);
              if (melt == _main_playing_media && !_playing_media)
                {
                   _media_play_set(_media_find_next(melt, EINA_FALSE), EINA_TRUE);
                }
              break;
           }
      case MEDIA_PLAYLIST:
           {
              Eina_List *itr;
              Media_Element *selt;
              EINA_LIST_FOREACH(melt->static_elts, itr, selt)
                {
                   selt->parent = melt;
                }
           }
      default: break;
     }
   _media_tree_sanitize(melt);
   _media_glitem_refresh(melt);
}

static void
_media_play_pause_cb(void *data EINA_UNUSED, Eo *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   if (!_selected_gl) return;
   Elm_Object_Item *sel = elm_genlist_selected_item_get(_selected_gl);
   Media_Element *melt = NULL;
   if (sel) melt = elm_object_item_data_get(sel);

   if (melt != _main_playing_media)
     {
        if (melt->type != MEDIA_JANGO)
          {
             _main_playing_media = melt;
             melt = _media_find_next(melt, EINA_TRUE);
             _media_play_set(melt, !melt->playing);
          }
        else
          {
             Eina_Tmpstr *dir;
             eina_file_mkdtemp("JANGO_XXXXXX", &dir);
             ecore_file_mkdir(dir);
             melt->jango = jango_session_new(melt->name, dir);
             melt->path = eina_stringshare_add(dir);
             melt->monitor = ecore_file_monitor_add(dir, _dir_update, melt);
             elm_object_text_set(_play_song_lb, "Loading...");
             _main_playing_media = melt;
             _playing_media = NULL;
          }
     }
   else if (_playing_media) _media_play_set(_playing_media, !_playing_media->playing);
}

static char *
_sl_format(double val)
{
   char str[100];
   sprintf(str, "%.2d:%.2d:%.2d", ((int)val) / 3600, (((int)val) % 3600) / 60, ((int)val) % 60);
   return strdup(str);
}

static void
_sl_label_free(char *str)
{
   free(str);
}

static void
_sl_changed(void *data EINA_UNUSED, const Efl_Event *ev EINA_UNUSED)
{
   double val = elm_slider_value_get(_play_prg_sl);
   emotion_object_position_set(_ply_emo, val);
}

static void
_media_add(void *data, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   Media_Type type = (intptr_t)data;
   Media_Element *m = efl_key_data_get(obj, "melt");
   Eo *entry = efl_key_data_get(obj, "entry");
   Eo *fs_entry = efl_key_data_get(obj, "fs_entry");

   if (!m)
     {
        m = calloc(1, sizeof(*m));
        m->type = type;
        _cfg->elements = eina_list_append(_cfg->elements, m);
     }
   if (m->path) eina_stringshare_del(m->path);
   if (fs_entry) m->path = eina_stringshare_add(elm_fileselector_path_get(fs_entry));
   if (m->name) eina_stringshare_del(m->name);
   const char *name = elm_entry_entry_get(entry);
   if (!name || !*name) name = "No name";
   m->name = eina_stringshare_add(name);
   _write_to_file(_cfg_filename);

   _media_list_clear(m);
   if (type == MEDIA_LIBRARY) _dir_update(m, NULL, ECORE_FILE_EVENT_NONE, NULL);
   _media_genlist_refresh();
   evas_object_del(_popup);
}

static void
_media_new_or_modify_show(Media_Element *melt, Media_Type mtype, Eina_Bool is_add)
{
   char title[100];
   if (!melt)
     {
        Elm_Object_Item *sel = elm_genlist_selected_item_get(_media_gl);
        melt = !is_add ? elm_object_item_data_get(sel) : NULL;
     }
   if (!is_add && !melt) return;

   _popup = elm_popup_add(_win);
   sprintf(title, "%s %s", is_add ? "Add" : "Edit",
         mtype == MEDIA_LIBRARY ? "library" :
         mtype == MEDIA_PLAYLIST ? "Playlist" :
         mtype == MEDIA_JANGO ? "Jango" :
         "unknown");
   elm_object_text_set(_popup, title);
   efl_weak_ref(&_popup);

   Eo *box = elm_box_add(_popup);
   evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_object_content_set(_popup, box);
   evas_object_show(box);

   Eo *title_box = elm_box_add(box);
   evas_object_size_hint_weight_set(title_box, EVAS_HINT_EXPAND, 0.2);
   evas_object_size_hint_align_set(title_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_horizontal_set(title_box, EINA_TRUE);
   elm_box_pack_end(box, title_box);
   evas_object_show(title_box);

   Eo *title_label = elm_label_add(title_box);
   evas_object_size_hint_weight_set(title_label, 0.2, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(title_label, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_object_text_set(title_label, "Name");
   elm_box_pack_end(title_box, title_label);
   evas_object_show(title_label);

   Eo *entry = elm_entry_add(title_box);
   evas_object_size_hint_weight_set(entry, 0.8, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(entry, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_entry_line_wrap_set(entry, ELM_WRAP_CHAR);
   elm_box_pack_end(title_box, entry);
   evas_object_show(entry);
   if (melt) elm_entry_entry_set(entry, melt->name);

   Eo *fs_entry = NULL;
   if (mtype == MEDIA_LIBRARY)
     {
        Eo *ic = elm_icon_add(box);
        elm_icon_standard_set(ic, "file");
        evas_object_size_hint_aspect_set(ic, EVAS_ASPECT_CONTROL_VERTICAL, 1, 1);
        fs_entry = elm_fileselector_entry_add(box);
        elm_object_part_content_set(fs_entry, "button icon", ic);
        evas_object_size_hint_weight_set(fs_entry, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
        evas_object_size_hint_align_set(fs_entry, EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_show(fs_entry);
        elm_box_pack_end(box, fs_entry);
        if (melt) elm_fileselector_path_set(fs_entry, melt->path);
     }

   Eo *add_bt = button_create(box, "Apply", NULL, NULL, _media_add, (void *)mtype);
   if (entry) efl_key_data_set(add_bt, "entry", entry);
   if (fs_entry) efl_key_data_set(add_bt, "fs_entry", fs_entry);
   elm_box_pack_end(box, add_bt);
   if (melt) efl_key_data_set(add_bt, "melt", melt);

   evas_object_show(_popup);
}

static void
_media_add_show(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Media_Type type = (intptr_t)data;
   _media_new_or_modify_show(NULL, type, EINA_TRUE);
}

static void
_media_edit_show(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Elm_Object_Item *sel = elm_genlist_selected_item_get(_media_gl);
   Media_Element *m = elm_object_item_data_get(sel);
   _media_new_or_modify_show(m, m->type, EINA_FALSE);
}

static void
_media_add_menu_show(void *data, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   Eo *mn = elm_menu_add(data);
   int x, y;
   evas_object_geometry_get(obj, &x, &y, NULL, NULL);
   elm_menu_item_add(mn, NULL, NULL, "Add library", _media_add_show, (void *)MEDIA_LIBRARY);
   elm_menu_item_add(mn, NULL, NULL, "Add playlist", _media_add_show, (void *)MEDIA_PLAYLIST);
   elm_menu_item_add(mn, NULL, NULL, "Add Jango link", _media_add_show, (void *)MEDIA_JANGO);
   elm_menu_move(mn, x, y);
   evas_object_show(mn);
}

static void
_media_del(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Elm_Object_Item *sel = elm_genlist_selected_item_get(_media_gl);
   if (!sel) return;
   Media_Element *m = elm_object_item_data_get(sel);
   _media_list_clear(m);
   if (m->parent)
     {
        m->parent->static_elts = eina_list_remove(m->parent->static_elts, m);
        m->parent->dynamic_elts = eina_list_remove(m->parent->dynamic_elts, m);
     }
   else
      _cfg->elements = eina_list_remove(_cfg->elements, m);
   _media_genlist_refresh();
   _write_to_file(_cfg_filename);
}

Eina_Bool
music_start(const char *filename, Eo *win)
{
   emotion_init();
   jango_init();

   _win = win;
   if (!_cfg) _eet_load();
   _cfg_filename = eina_stringshare_add(filename);
   Eet_File *file = eet_open(_cfg_filename, EET_FILE_MODE_READ);
   if (file)
     {
        _cfg = eet_data_read(file, _music_edd, "entry");
        eet_close(file);
     }
   else
     {
        /* Default path */
        char *home_dir = getenv("HOME");
        char dflt_path[1024];
        if (!_cfg) _cfg = calloc(1, sizeof(*_cfg));
        Media_Element *melt = calloc(1, sizeof(*melt));
        melt->type = MEDIA_LIBRARY;
        sprintf(dflt_path, "%s/Music", home_dir);
        melt->name = eina_stringshare_add("Music");
        melt->path = eina_stringshare_add(dflt_path);
        _cfg->elements = eina_list_append(_cfg->elements, melt);
     }
   _write_to_file(_cfg_filename);

   if (!_ply_emo)
     {
        _ply_emo = emotion_object_add(win);
        efl_weak_ref(&_ply_emo);
        emotion_object_init(_ply_emo, NULL);
        efl_event_callback_add
           (_ply_emo, EFL_CANVAS_VIDEO_EVENT_LENGTH_CHANGE, _media_length_update, NULL);
        efl_event_callback_add
           (_ply_emo, EFL_CANVAS_VIDEO_EVENT_POSITION_CHANGE, _media_position_update, NULL);
        efl_event_callback_add
           (_ply_emo, EFL_CANVAS_VIDEO_EVENT_PLAYBACK_STOP, _media_finished, NULL);
     }

   Eina_List *itr;
   Media_Element *melt;
   EINA_LIST_FOREACH(_cfg->elements, itr, melt)
     {
        if (melt->type == MEDIA_LIBRARY)
           melt->monitor = ecore_file_monitor_add(melt->path, _dir_update, melt);
        _dir_update(melt, NULL, ECORE_FILE_EVENT_NONE, NULL);
     }

   return EINA_TRUE;
}

Eina_Bool
music_stop(void)
{
   if (_cfg)
     {
        Media_Element *melt;
        EINA_LIST_FREE(_cfg->elements, melt)
          {
             _media_element_del(melt);
          }
     }
   efl_del(_ply_emo);
   emotion_shutdown();
   return EINA_TRUE;
}

static Elm_Object_Item *
_gl_item_getcb(Evas_Object *obj, Evas_Coord x, Evas_Coord y, int *xposret EINA_UNUSED, int *yposret)
{  /* This function returns pointer to item under (x,y) coords */
   printf("<%s> <%d> obj=<%p>\n", __func__, __LINE__, obj);
   Elm_Object_Item *gli;
   gli = elm_genlist_at_xy_item_get(obj, x, y, yposret);
   if (gli)
     printf("over <%s>, gli=<%p> yposret %i\n",
           (char *)elm_object_item_data_get(gli), gli, *yposret);
   else
     printf("over none, yposret %i\n", *yposret);
   return gli;
}

static inline char *
_strndup(const char *str, size_t len)
{
   size_t slen = strlen(str);
   char *ret;

   if (slen > len) slen = len;
   ret = malloc (slen + 1);
   if (!ret) return NULL;

   if (slen > 0) memcpy(ret, str, slen);
   ret[slen] = '\0';
   return ret;
}

static char *
_drag_data_extract(char **drag_data)
{
   char *uri = NULL;
   if (!drag_data)
     return uri;

   char *p = *drag_data;
   if (!p)
     return uri;
   char *s = strstr(p, FILESEP);
   if (s)
     p += FILESEP_LEN;
   s = strchr(p, '\n');
   uri = p;
   if (s)
     {
        if (s - p > 0)
          {
             char *s1 = s - 1;
             if (s1[0] == '\r')
               s1[0] = '\0';
             else
               {
                  char *s2 = s + 1;
                  if (s2[0] == '\r')
                    {
                       s[0] = '\0';
                       s++;
                    }
                  else
                    s[0] = '\0';
               }
          }
        else
          s[0] = '\0';
        s++;
     }
   else
     p = NULL;
   *drag_data = s;

   return uri;
}

static Eina_Bool
_gl_dropcb(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, Elm_Object_Item *over_it, Elm_Selection_Data *ev, int xposret EINA_UNUSED, int yposret)
{  /* This function is called when data is dropped on the genlist */
   printf("<%s> <%d> str=<%s>\n", __func__, __LINE__, (char *) ev->data);
   if (!ev->data) return EINA_FALSE;
   if (ev->len <= 0) return EINA_FALSE;

   char *dd = _strndup(ev->data, ev->len);
   if (!dd) return EINA_FALSE;
   char *p = dd;

   char *s = _drag_data_extract(&p);
   Media_Element *over_elt = elm_object_item_data_get(over_it);
   while (s)
     {
        char *name = strrchr(s, '/');
        Media_Element *melt = calloc(1, sizeof(*melt));
        melt->type = MEDIA_URI;
        melt->path = strdup(s);
        melt->name = name ? strdup(name + 1) : strdup(s);
        if (over_elt->type == MEDIA_PLAYLIST)
          {
             melt->parent = over_elt;
             over_elt->static_elts = eina_list_append(over_elt->static_elts, melt);
             over_elt->expanded = EINA_TRUE;
          }
        else if (over_elt->type == MEDIA_URI)
          {
             Media_Element *par_elt = over_elt->parent;
             if (par_elt && par_elt->type == MEDIA_PLAYLIST)
               {
                  melt->parent = par_elt;
                  switch(yposret)
                    {
                     case -1:  /* Dropped on top-part of the over_it item */
                          {
                             par_elt->static_elts = eina_list_prepend_relative
                                (par_elt->static_elts, melt, over_elt);
                             break;
                          }
                     case  0:  /* Dropped on center of the it item      */
                     case  1:  /* Dropped on botton-part of the it item */
                          {
                             par_elt->static_elts = eina_list_append_relative
                                (par_elt->static_elts, melt, over_elt);
                             break;
                          }
                     default:
                          {
                             free(dd);
                             return EINA_FALSE;
                          }
                    }
               }
          }

        s = _drag_data_extract(&p);
     }
   free(dd);

   _media_genlist_refresh();
   _write_to_file(_cfg_filename);

   return EINA_TRUE;
}

static Evas_Object *
_gl_createicon(void *data, Evas_Object *win, Evas_Coord *xoff, Evas_Coord *yoff)
{
   printf("<%s> <%d>\n", __func__, __LINE__);
   Evas_Object *icon = NULL;
   Evas_Object *o = elm_object_item_part_content_get(data, "elm.swallow.icon");

   if (o)
     {
        int xm, ym, w = 30, h = 30;
        const char *f;
        const char *g;
        elm_image_file_get(o, &f, &g);
        evas_pointer_canvas_xy_get(evas_object_evas_get(o), &xm, &ym);
        if (xoff) *xoff = xm - (w/2);
        if (yoff) *yoff = ym - (h/2);
        icon = elm_icon_add(win);
        elm_image_file_set(icon, f, g);
        evas_object_size_hint_align_set(icon,
              EVAS_HINT_FILL, EVAS_HINT_FILL);
        evas_object_size_hint_weight_set(icon,
              EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
        if (xoff && yoff) evas_object_move(icon, *xoff, *yoff);
        evas_object_resize(icon, w, h);
     }

   return icon;
}

static int
_item_ptr_cmp(const void *d1, const void *d2)
{
   return ((const char *) d1 - (const char *) d2);
}

static Eina_List *
_gl_icons_get(void *data)
{  /* Start icons animation before actually drag-starts */
   printf("<%s> <%d>\n", __func__, __LINE__);
   int yposret = 0;

   Eina_List *l;

   Eina_List *icons = NULL;

   Evas_Coord xm, ym;
   evas_pointer_canvas_xy_get(evas_object_evas_get(data), &xm, &ym);
   Eina_List *items = eina_list_clone(elm_genlist_selected_items_get(data));
   Elm_Object_Item *gli = elm_genlist_at_xy_item_get(data,
         xm, ym, &yposret);
   if (gli)
     {  /* Add the item mouse is over to the list if NOT seleced */
        void *p = eina_list_search_unsorted(items, _item_ptr_cmp, gli);
        if (!p)
          items = eina_list_append(items, gli);
     }

   EINA_LIST_FOREACH(items, l, gli)
     {
        Media_Element *melt = elm_object_item_data_get(gli);
        if (melt->type != MEDIA_URI) continue;
        /* Now add icons to animation window */
        Evas_Object *o = elm_object_item_part_content_get(gli,
              "elm.swallow.icon");

        if (o)
          {
             int x, y, w, h;
             const char *f, *g;
             elm_image_file_get(o, &f, &g);
             Evas_Object *ic = elm_icon_add(data);
             elm_image_file_set(ic, f, g);
             evas_object_geometry_get(o, &x, &y, &w, &h);
             evas_object_size_hint_align_set(ic,
                   EVAS_HINT_FILL, EVAS_HINT_FILL);
             evas_object_size_hint_weight_set(ic,
                   EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);

             evas_object_move(ic, x, y);
             evas_object_resize(ic, w, h);
             evas_object_show(ic);

             icons =  eina_list_append(icons, ic);
          }
     }

   eina_list_free(items);
   return icons;
}

static const char *
_drag_data_build(Eina_List **items)
{
   const char *drag_data = NULL;
   if (*items)
     {
        Eina_Strbuf *str;
        Eina_List *l;
        Elm_Object_Item *it;
        int i = 0;

        str = eina_strbuf_new();
        if (!str) return NULL;

        /* drag data in form: file://URI1\nfile://URI2 */
        EINA_LIST_FOREACH(*items, l, it)
          {
             Media_Element *melt = elm_object_item_data_get(it);
             if (melt && melt->type == MEDIA_URI)
               {
                  if (i > 0) eina_strbuf_append(str, "\n");
                  eina_strbuf_append(str, FILESEP);
                  eina_strbuf_append(str, melt->path);
                  i++;
               }
          }
        drag_data = eina_strbuf_string_steal(str);
        eina_strbuf_free(str);
     }
   return drag_data;
}

static const char *
_gl_get_drag_data(Evas_Object *obj, Elm_Object_Item *it, Eina_List **items)
{  /* Construct a string of dragged info, user frees returned string */
   const char *drag_data = NULL;
   printf("<%s> <%d>\n", __func__, __LINE__);

   *items = eina_list_clone(elm_genlist_selected_items_get(obj));
   if (it)
     {  /* Add the item mouse is over to the list if NOT seleced */
        void *p = eina_list_search_unsorted(*items, _item_ptr_cmp, it);
        if (!p)
          *items = eina_list_append(*items, it);
     }
   drag_data = _drag_data_build(items);
   printf("<%s> <%d> Sending <%s>\n", __func__, __LINE__, drag_data);

   return drag_data;
}

static void
_gl_dragdone(void *data, Evas_Object *obj EINA_UNUSED, Eina_Bool doaccept)
{
   printf("<%s> <%d> data=<%p> doaccept=<%d>\n",
         __func__, __LINE__, data, doaccept);

   if (doaccept)
     {  /* Remove items dragged out (accepted by target) */
#if 0
        EINA_LIST_FOREACH(data, l, it)
           elm_object_item_del(it);
#endif
     }

   eina_list_free(data);
   return;
}

static Eina_Bool
_gl_dnd_default_anim_data_getcb(Evas_Object *obj,  /* The genlist object */
      Elm_Object_Item *it,
      Elm_Drag_User_Info *info)
{  /* This called before starting to drag, mouse-down was on it */
   info->format = ELM_SEL_FORMAT_TARGETS;
   info->createicon = _gl_createicon;
   info->createdata = it;
   info->icons = _gl_icons_get(obj);
   info->dragdone = _gl_dragdone;

   /* Now, collect data to send for drop from ALL selected items */
   /* Save list pointer to remove items after drop and free list on done */
   info->data = _gl_get_drag_data(obj, it, (Eina_List **) &info->donecbdata);
   printf("%s - data = %s\n", __FUNCTION__, info->data);
   info->acceptdata = info->donecbdata;

   return !!info->data;
}

Eo *
music_ui_get(Eo *parent)
{
   /* Main box */
   Eo *box = elm_box_add(parent);
   evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_show(box);

   /* Media and playlist box */
   Eo *list_box = elm_box_add(box);
   evas_object_size_hint_weight_set(list_box, EVAS_HINT_EXPAND, 0.9);
   evas_object_size_hint_align_set(list_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_horizontal_set(list_box, EINA_TRUE);
   elm_box_pack_end(box, list_box);
   evas_object_show(list_box);

   /* Media box */
   Eo *media_box = elm_box_add(list_box);
   evas_object_size_hint_weight_set(media_box, 0.4, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(media_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(list_box, media_box);
   evas_object_show(media_box);

   /* Add/del/edit dirs buttons box */
   Eo *bts_box = elm_box_add(media_box);
   elm_box_horizontal_set(bts_box, EINA_TRUE);
   evas_object_size_hint_weight_set(bts_box, EVAS_HINT_EXPAND, 0.05);
   evas_object_size_hint_align_set(bts_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(media_box, bts_box);
   evas_object_show(bts_box);

   /* Add/del/edit dirs buttons */
   elm_box_pack_end(bts_box,
         button_create(bts_box, NULL,
            icon_create(bts_box, "list-add", NULL),
            NULL, _media_add_menu_show, parent));
   elm_box_pack_end(bts_box,
         button_create(bts_box, NULL,
            icon_create(bts_box, "document-properties", NULL),
            NULL, _media_edit_show, NULL));
   elm_box_pack_end(bts_box, button_create(bts_box, NULL,
            icon_create(bts_box, "list-remove", NULL),
            NULL, _media_del, NULL));

   /* Media genlist */
   _media_gl = elm_genlist_add(media_box);
   evas_object_size_hint_weight_set(_media_gl, EVAS_HINT_EXPAND, 0.95);
   evas_object_size_hint_align_set(_media_gl, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(media_box, _media_gl);
   evas_object_show(_media_gl);
   efl_wref_add(_media_gl, &_selected_gl);

   elm_drop_item_container_add(_media_gl, ELM_SEL_FORMAT_TARGETS, _gl_item_getcb, NULL, NULL,
         NULL, NULL, NULL, NULL, _gl_dropcb, NULL);
   elm_drag_item_container_add(_media_gl, 0.5, 0.3,
         _gl_item_getcb, _gl_dnd_default_anim_data_getcb);

   evas_object_smart_callback_add(_media_gl, "expand,request", _expand_req, NULL);
   evas_object_smart_callback_add(_media_gl, "contract,request", _contract_req, NULL);
   evas_object_smart_callback_add(_media_gl, "expanded", _expand, NULL);
   evas_object_smart_callback_add(_media_gl, "contracted", _contract, NULL);

   _media_genlist_refresh();

   /* Player vertical box */
   Eo *ply_box = elm_box_add(box);
   evas_object_size_hint_weight_set(ply_box, EVAS_HINT_EXPAND, 0.1);
   evas_object_size_hint_align_set(ply_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(box, ply_box);
   evas_object_show(ply_box);

   /* Player slider horizontal box */
   Eo *ply_sl_box = elm_box_add(ply_box);
   evas_object_size_hint_weight_set(ply_sl_box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(ply_sl_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_horizontal_set(ply_sl_box, EINA_TRUE);
   elm_box_pack_end(ply_box, ply_sl_box);
   evas_object_show(ply_sl_box);

   /* Label showing music progress */
   _play_prg_lb = elm_label_add(ply_sl_box);
   evas_object_size_hint_align_set(_play_prg_lb, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(_play_prg_lb, 0.1, EVAS_HINT_EXPAND);
   _media_position_update(_ply_emo, NULL);
   elm_box_pack_end(ply_sl_box, _play_prg_lb);
   efl_weak_ref(&_play_prg_lb);
   evas_object_show(_play_prg_lb);

   /* Slider showing music progress */
   _play_prg_sl = elm_slider_add(ply_sl_box);
   elm_slider_indicator_format_function_set(_play_prg_sl, _sl_format, _sl_label_free);
   elm_slider_span_size_set(_play_prg_sl, 120);
   efl_event_callback_add(_play_prg_sl, ELM_SLIDER_EVENT_CHANGED, _sl_changed, NULL);
   evas_object_size_hint_align_set(_play_prg_sl, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(_play_prg_sl, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   elm_box_pack_end(ply_sl_box, _play_prg_sl);
   efl_weak_ref(&_play_prg_sl);
   evas_object_show(_play_prg_sl);

   /* Label showing total music time */
   _play_total_lb = elm_label_add(ply_sl_box);
   evas_object_size_hint_align_set(_play_total_lb, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(_play_total_lb, 0.1, EVAS_HINT_EXPAND);
   _media_length_update(_ply_emo, NULL);
   elm_box_pack_end(ply_sl_box, _play_total_lb);
   efl_weak_ref(&_play_total_lb);
   evas_object_show(_play_total_lb);

   /* Player song name */
   _play_song_lb = elm_label_add(ply_box);
   evas_object_size_hint_align_set(_play_song_lb, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(_play_song_lb, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   elm_box_pack_end(ply_box, _play_song_lb);
   efl_weak_ref(&_play_song_lb);
   evas_object_show(_play_song_lb);

   /* Player buttons box */
   Eo *ply_bts_box = elm_box_add(ply_box);
   evas_object_size_hint_weight_set(ply_bts_box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(ply_bts_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_horizontal_set(ply_bts_box, EINA_TRUE);
   elm_box_pack_end(ply_box, ply_bts_box);
   evas_object_show(ply_bts_box);

   /* Play/pause button */
   _play_bt = button_create(ply_bts_box, NULL,
         icon_create(ply_bts_box,
            _playing_media && _playing_media->playing?"media-playback-pause":"media-playback-start", NULL),
         NULL, _media_play_pause_cb, NULL);
   elm_box_pack_end(ply_bts_box, _play_bt);
   efl_weak_ref(&_play_bt);

   return box;
}


