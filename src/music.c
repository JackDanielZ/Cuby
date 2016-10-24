#define EFL_BETA_API_SUPPORT
#define EFL_EO_API_SUPPORT
#include <Eo.h>
#include <Eet.h>
#include <Ecore.h>
#include <Elementary.h>
#include <Emotion.h>

#include "common.h"

static Eet_Data_Descriptor *_music_edd = NULL;

typedef enum
{
   MEDIA_LIBRARY = 1,
   MEDIA_PLAYLIST = 2,
   MEDIA_URI = 3
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
   Media_Element *parent;
   Eina_List *dynamic_elts;
   Eina_Bool expanded : 1;
   Eina_Bool playing : 1;
};

typedef struct
{
   Eina_List *elements; /* List of Media_Element */
} Music_Cfg;

static Music_Cfg *_cfg = NULL;
static Eina_Hash *_full_paths_hash = NULL;

static Eo *_win = NULL, *_media_gl = NULL, *_popup = NULL;
static Eina_Stringshare *_cfg_filename = NULL;
static Elm_Genlist_Item_Class *_media_itc = NULL;

static Eo *_ply_emo = NULL, *_play_total_lb = NULL, *_play_prg_lb = NULL, *_play_prg_sl = NULL;
static Eo *_play_bt = NULL;
static Media_Element *_file_playing = NULL;

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
_media_genlist_refresh()
{
   if (!_media_itc)
     {
        _media_itc = elm_genlist_item_class_new();
        _media_itc->item_style = "default";
        _media_itc->func.text_get = _media_text_get;
     }

   elm_genlist_clear(_media_gl);
   Eina_List *itr;
   Media_Element *melt;
   EINA_LIST_FOREACH(_cfg->elements, itr, melt)
     {
        Elm_Object_Item *it = elm_genlist_item_append(_media_gl, _media_itc, melt, NULL,
              melt->static_elts || melt->dynamic_elts?ELM_GENLIST_ITEM_TREE:ELM_GENLIST_ITEM_NONE,
              NULL, NULL);
        elm_genlist_item_expanded_set(it, melt->expanded);
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
        Elm_Object_Item *it = elm_genlist_item_append(_media_gl, _media_itc, selt, glit,
              selt->static_elts || selt->dynamic_elts?ELM_GENLIST_ITEM_TREE:ELM_GENLIST_ITEM_NONE,
              NULL, NULL);
        elm_genlist_item_expanded_set(it, selt->expanded);
     }
   EINA_LIST_FOREACH(melt->dynamic_elts, itr, selt)
     {
        Elm_Object_Item *it = elm_genlist_item_append(_media_gl, _media_itc, selt, glit,
              selt->static_elts || selt->dynamic_elts?ELM_GENLIST_ITEM_TREE:ELM_GENLIST_ITEM_NONE,
              NULL, NULL);
        elm_genlist_item_expanded_set(it, selt->expanded);
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
_files_scan(Media_Element *melt)
{
   if (melt->type == MEDIA_LIBRARY || melt->type == MEDIA_URI)
     {
        Eina_List *lst = ecore_file_ls(melt->path);
        char *name;
        EINA_LIST_FREE(lst, name)
          {
             char full_path[1024];
             Media_Element *selt = calloc(1, sizeof(*selt));
             selt->type = MEDIA_URI;
             selt->parent = melt;
             sprintf(full_path, "%s/%s", melt->path, name);
             selt->path = eina_stringshare_add(full_path);
             selt->name = eina_stringshare_add(name);
             eina_hash_set(_full_paths_hash, selt->path, selt);
             if (ecore_file_is_dir(full_path)) _files_scan(selt);
             melt->dynamic_elts = eina_list_append(melt->dynamic_elts, selt);
             free(name);
          }
     }
   else if (melt->type == MEDIA_PLAYLIST)
     {
        Eina_List *itr;
        Media_Element *selt;
        EINA_LIST_FOREACH(melt->static_elts, itr, selt)
          {
             selt->parent = melt;
          }
     }
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
_media_element_free(Media_Element *melt)
{
   Media_Element *selt;
   if (!melt) return;
   EINA_LIST_FREE(melt->static_elts, selt)
     {
        _media_element_free(selt);
     }
   EINA_LIST_FREE(melt->dynamic_elts, selt)
     {
        _media_element_free(selt);
     }
   free(melt);
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

static void
_media_finished(void *data EINA_UNUSED, const Efl_Event *ev EINA_UNUSED)
{
   _file_playing->playing = EINA_FALSE;
   elm_object_part_content_set(_play_bt, "icon",
         icon_create(_play_bt, "media-playback-start", NULL));
}

static void
_media_play_pause_cb(void *data EINA_UNUSED, Eo *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   if (!_selected_gl) return;
   Elm_Object_Item *sel = elm_genlist_selected_item_get(_selected_gl);
   Media_Element *melt = NULL;
   if (sel) melt = elm_object_item_data_get(sel);

   /* The selected path is different of the played path */
   if (melt && melt != _file_playing)
     {
        emotion_object_play_set(_ply_emo, EINA_FALSE);
        if (_file_playing) _file_playing->playing = EINA_FALSE;
        _file_playing = melt;
        emotion_object_file_set(_ply_emo, _file_playing->path);
     }

   /* Play again when finished - int conversion is needed because the returned values are not
    * exactly the same. */
   if ((int)emotion_object_position_get(_ply_emo) == (int)emotion_object_play_length_get(_ply_emo))
      emotion_object_position_set(_ply_emo, 0);

   if (_file_playing)
     {
        _file_playing->playing = !_file_playing->playing;
        emotion_object_play_set(_ply_emo, _file_playing->playing?EINA_TRUE:EINA_FALSE);
        elm_object_part_content_set(_play_bt, "icon",
              icon_create(_play_bt,
                 _file_playing->playing?"media-playback-pause":"media-playback-start",
                 NULL));
     }
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
   if (type == MEDIA_LIBRARY) _files_scan(m);
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
         mtype == MEDIA_PLAYLIST ? "Playlist" : "unknown");
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

   _full_paths_hash = eina_hash_stringshared_new(NULL);

   Eina_List *itr;
   Media_Element *melt;
   EINA_LIST_FOREACH(_cfg->elements, itr, melt)
     {
        _files_scan(melt);
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
             _media_element_free(melt);
          }
     }
   eina_hash_free(_full_paths_hash);
   _full_paths_hash = NULL;
   efl_del(_ply_emo);
   emotion_shutdown();
   return EINA_TRUE;
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
            _file_playing && _file_playing->playing?"media-playback-pause":"media-playback-start", NULL),
         NULL, _media_play_pause_cb, NULL);
   elm_box_pack_end(ply_bts_box, _play_bt);
   efl_weak_ref(&_play_bt);

   return box;
}


