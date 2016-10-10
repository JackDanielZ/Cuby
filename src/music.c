#define EFL_BETA_API_SUPPORT
#define EFL_EO_API_SUPPORT
#include <Eo.h>
#include <Eet.h>
#include <Ecore.h>
#include <Elementary.h>
#include <Emotion.h>

#include "common.h"

static Eet_Data_Descriptor *_music_edd = NULL;

typedef struct
{
   const char *name;
   const char *path;
   /* Not in eet */
   Eina_List *files;
   Eina_Bool expanded:1;
   Eina_Bool playing:1;
} Music_Path;

typedef struct
{
   Eina_List *paths_lst;
} Music_Cfg;

static Music_Cfg *_cfg = NULL;
static Eo *_win = NULL, *_paths_gl = NULL, *_popup = NULL;
static Eina_Stringshare *_cfg_filename = NULL;
static Elm_Genlist_Item_Class *_itc = NULL;

static Eo *_playlist_gl = NULL;

static Eo *_ply_emo = NULL, *_play_total_lb = NULL, *_play_prg_lb = NULL, *_play_prg_sl = NULL;
static Eo *_play_bt = NULL;
static Music_Path *_file_playing = NULL;

static void
_eet_load()
{
   Eet_Data_Descriptor_Class eddc;
   if (!_music_edd)
     {
        EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Music_Path);
        Eet_Data_Descriptor *music_path_edd = eet_data_descriptor_stream_new(&eddc);

#define CFG_ADD_BASIC(member, eet_type)\
        EET_DATA_DESCRIPTOR_ADD_BASIC\
        (music_path_edd, Music_Path, # member, member, eet_type)

        CFG_ADD_BASIC(name, EET_T_STRING);
        CFG_ADD_BASIC(path, EET_T_STRING);

        EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Music_Cfg);
        _music_edd = eet_data_descriptor_stream_new(&eddc);
        EET_DATA_DESCRIPTOR_ADD_LIST(_music_edd, Music_Cfg, "paths_lst", paths_lst, music_path_edd);
#undef CFG_ADD_BASIC
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
_text_get(void *data, Evas_Object *obj EINA_UNUSED, const char *part EINA_UNUSED)
{
   Music_Path *path = data;
   return strdup(path->name?path->name:path->path);
}

static void
_paths_genlist_refresh()
{
   if (!_itc)
     {
        _itc = elm_genlist_item_class_new();
        _itc->item_style = "default";
        _itc->func.text_get = _text_get;
     }

   elm_genlist_clear(_paths_gl);
   Eina_List *itr;
   Music_Path *path;
   EINA_LIST_FOREACH(_cfg->paths_lst, itr, path)
     {
        Elm_Object_Item *it = elm_genlist_item_append(_paths_gl, _itc, path, NULL,
              path->files?ELM_GENLIST_ITEM_TREE:ELM_GENLIST_ITEM_NONE,
              NULL, NULL);
        elm_genlist_item_expanded_set(it, path->expanded);
     }
}

static void
_expand(void *data EINA_UNUSED, Evas_Object *cont EINA_UNUSED, void *event_info)
{
   Elm_Object_Item *glit = event_info;
   Music_Path *path = elm_object_item_data_get(glit);
   Eina_List *itr;
   path->expanded = EINA_TRUE;
   EINA_LIST_FOREACH(path->files, itr, path)
     {
        Elm_Object_Item *it = elm_genlist_item_append(_paths_gl, _itc, path, glit,
              path->files?ELM_GENLIST_ITEM_TREE:ELM_GENLIST_ITEM_NONE,
              NULL, NULL);
        elm_genlist_item_expanded_set(it, path->expanded);
     }
}

static void
_contract(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Elm_Object_Item *glit = event_info;
   Music_Path *path = elm_object_item_data_get(glit);
   elm_genlist_item_subitems_clear(glit);
   path->expanded = EINA_FALSE;
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
_files_scan(Music_Path *path)
{
   Eina_List *lst = ecore_file_ls(path->path);
   char *name;
   EINA_LIST_FREE(lst, name)
     {
        char full_path[1024];
        Music_Path *fpath = calloc(1, sizeof(*fpath));
        sprintf(full_path, "%s/%s", path->path, name);
        fpath->path = eina_stringshare_add(full_path);
        fpath->name = eina_stringshare_add(name);
        if (ecore_file_is_dir(full_path)) _files_scan(fpath);
        path->files = eina_list_append(path->files, fpath);
        free(name);
     }
}

static void
_files_list_clear(Music_Path *path)
{
   Music_Path *fpath;
   EINA_LIST_FREE(path->files, fpath)
     {
        _files_list_clear(fpath);
        free(fpath);
     }
}

static void
_music_path_free(Music_Path *path)
{
   Music_Path *fpath;
   if (!path) return;
   EINA_LIST_FREE(path->files, fpath)
     {
        _music_path_free(fpath);
     }
   free(path);
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
   Elm_Object_Item *sel = elm_genlist_selected_item_get(_paths_gl);
   Music_Path *path = sel ? elm_object_item_data_get(sel) : NULL;

   /* The selected path is different of the played path */
   if (path && path != _file_playing)
     {
        emotion_object_play_set(_ply_emo, EINA_FALSE);
        if (_file_playing) _file_playing->playing = EINA_FALSE;
        _file_playing = path;
        emotion_object_file_set(_ply_emo, _file_playing->path);
     }

   /* Play again when finished - int conversion is needed because the returned values are not
    * exactly the same. */
   if ((int)emotion_object_position_get(_ply_emo) == (int)emotion_object_play_length_get(_ply_emo))
      emotion_object_position_set(_ply_emo, 0);

   _file_playing->playing = !_file_playing->playing;
   emotion_object_play_set(_ply_emo, _file_playing->playing?EINA_TRUE:EINA_FALSE);
   elm_object_part_content_set(_play_bt, "icon",
         icon_create(_play_bt,
            _file_playing->playing?"media-playback-pause":"media-playback-start",
            NULL));
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
_dir_add(void *data EINA_UNUSED, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   Music_Path *m = efl_key_data_get(obj, "mpath");
   Eo *entry = efl_key_data_get(obj, "entry");
   Eo *fs_entry = efl_key_data_get(obj, "fs_entry");

   if (!m)
     {
        m = calloc(1, sizeof(*m));
        _cfg->paths_lst = eina_list_append(_cfg->paths_lst, m);
     }
   if (m->path) eina_stringshare_del(m->path);
   m->path = eina_stringshare_add(elm_fileselector_path_get(fs_entry));
   if (m->name) eina_stringshare_del(m->name);
   const char *name = elm_entry_entry_get(entry);
   if (!name || !*name) name = "No name";
   m->name = eina_stringshare_add(name);
   _write_to_file(_cfg_filename);

   _files_list_clear(m);
   _files_scan(m);
   _paths_genlist_refresh();
   evas_object_del(_popup);
}

static void
_dir_add_show(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Eina_Bool is_add = (Eina_Bool)(intptr_t)data;

   Elm_Object_Item *sel = elm_genlist_selected_item_get(_paths_gl);
   Music_Path *m = !is_add ? elm_object_item_data_get(sel) : NULL;
   if (!is_add && !m) return;

   _popup = elm_popup_add(_win);
   elm_object_text_set(_popup, is_add?"Add directory":"Edit directory");
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
   elm_object_text_set(title_label, "Title");
   elm_box_pack_end(title_box, title_label);
   evas_object_show(title_label);

   Eo *entry = elm_entry_add(title_box);
   evas_object_size_hint_weight_set(entry, 0.8, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(entry, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_entry_line_wrap_set(entry, ELM_WRAP_CHAR);
   elm_box_pack_end(title_box, entry);
   evas_object_show(entry);

   Eo *ic = elm_icon_add(box);
   elm_icon_standard_set(ic, "file");
   evas_object_size_hint_aspect_set(ic, EVAS_ASPECT_CONTROL_VERTICAL, 1, 1);
   Eo *fs_entry = elm_fileselector_entry_add(box);
   elm_object_part_content_set(fs_entry, "button icon", ic);
   evas_object_size_hint_weight_set(fs_entry, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(fs_entry, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_show(fs_entry);
   elm_box_pack_end(box, fs_entry);

   Eo *add_bt = button_create(box, "Apply", NULL, NULL, _dir_add, NULL);
   efl_key_data_set(add_bt, "entry", entry);
   efl_key_data_set(add_bt, "fs_entry", fs_entry);
   elm_box_pack_end(box, add_bt);

   if (m)
     {
        elm_entry_entry_set(entry, m->name);
        elm_fileselector_path_set(fs_entry, m->path);
        efl_key_data_set(add_bt, "mpath", m);
     }
   evas_object_show(_popup);
}

static void
_dir_del(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Elm_Object_Item *sel = elm_genlist_selected_item_get(_paths_gl);
   if (!sel) return;
   Music_Path *m = elm_object_item_data_get(sel);
   _files_list_clear(m);
   _cfg->paths_lst = eina_list_remove(_cfg->paths_lst, m);
   _paths_genlist_refresh();
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
        char *home_dir = getenv("HOME");
        char dflt_path[1024];
        if (!_cfg) _cfg = calloc(1, sizeof(*_cfg));
        Music_Path *path = calloc(1, sizeof(*path));
        sprintf(dflt_path, "%s/Music", home_dir);
        path->name = eina_stringshare_add("Music");
        path->path = eina_stringshare_add(dflt_path);
        _cfg->paths_lst = eina_list_append(_cfg->paths_lst, path);
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
   Music_Path *path;
   EINA_LIST_FOREACH(_cfg->paths_lst, itr, path)
     {
        _files_scan(path);
     }

   return EINA_TRUE;
}

Eina_Bool
music_stop(void)
{
   if (_cfg)
     {
        Music_Path *path;
        EINA_LIST_FREE(_cfg->paths_lst, path)
          {
             _music_path_free(path);
          }
     }
   efl_del(_ply_emo);
   emotion_shutdown();
   return EINA_TRUE;
}

Eo *
music_ui_get(Eo *parent)
{
   Eo *box = elm_box_add(parent);
   evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_show(box);

   Eo *list_box = elm_box_add(box);
   evas_object_size_hint_weight_set(list_box, EVAS_HINT_EXPAND, 0.9);
   evas_object_size_hint_align_set(list_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_horizontal_set(list_box, EINA_TRUE);
   elm_box_pack_end(box, list_box);
   evas_object_show(list_box);

   Eo *paths_box = elm_box_add(list_box);
   evas_object_size_hint_weight_set(paths_box, 0.5, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(paths_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(list_box, paths_box);
   evas_object_show(paths_box);

   Eo *bts_box = elm_box_add(paths_box);
   elm_box_horizontal_set(bts_box, EINA_TRUE);
   evas_object_size_hint_weight_set(bts_box, EVAS_HINT_EXPAND, 0.05);
   evas_object_size_hint_align_set(bts_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(paths_box, bts_box);
   evas_object_show(bts_box);

   elm_box_pack_end(bts_box,
         button_create(bts_box, "Add directory", NULL, NULL, _dir_add_show, (void *)EINA_TRUE));
   elm_box_pack_end(bts_box,
         button_create(bts_box, "Edit directory", NULL, NULL, _dir_add_show, (void *)EINA_FALSE));
   elm_box_pack_end(bts_box, button_create(bts_box, "Delete directory", NULL, NULL, _dir_del, NULL));

   _paths_gl = elm_genlist_add(paths_box);
   evas_object_size_hint_weight_set(_paths_gl, EVAS_HINT_EXPAND, 0.95);
   evas_object_size_hint_align_set(_paths_gl, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(paths_box, _paths_gl);
   evas_object_show(_paths_gl);

   evas_object_smart_callback_add(_paths_gl, "expand,request", _expand_req, NULL);
   evas_object_smart_callback_add(_paths_gl, "contract,request", _contract_req, NULL);
   evas_object_smart_callback_add(_paths_gl, "expanded", _expand, NULL);
   evas_object_smart_callback_add(_paths_gl, "contracted", _contract, NULL);

   _paths_genlist_refresh();

   _playlist_gl = elm_genlist_add(list_box);
   evas_object_size_hint_weight_set(_playlist_gl, 0.5, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(_playlist_gl, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(list_box, _playlist_gl);
   evas_object_show(_playlist_gl);

   Eo *ply_box = elm_box_add(box);
   evas_object_size_hint_weight_set(ply_box, EVAS_HINT_EXPAND, 0.1);
   evas_object_size_hint_align_set(ply_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(box, ply_box);
   evas_object_show(ply_box);

   Eo *ply_sl_box = elm_box_add(ply_box);
   evas_object_size_hint_weight_set(ply_sl_box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(ply_sl_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_horizontal_set(ply_sl_box, EINA_TRUE);
   elm_box_pack_end(ply_box, ply_sl_box);
   evas_object_show(ply_sl_box);

   _play_prg_lb = elm_label_add(ply_sl_box);
   evas_object_size_hint_align_set(_play_prg_lb, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(_play_prg_lb, 0.1, EVAS_HINT_EXPAND);
   _media_position_update(_ply_emo, NULL);
   elm_box_pack_end(ply_sl_box, _play_prg_lb);
   efl_weak_ref(&_play_prg_lb);
   evas_object_show(_play_prg_lb);

   _play_prg_sl = elm_slider_add(ply_sl_box);
   elm_slider_indicator_format_function_set(_play_prg_sl, _sl_format, _sl_label_free);
   elm_slider_span_size_set(_play_prg_sl, 120);
   efl_event_callback_add(_play_prg_sl, ELM_SLIDER_EVENT_CHANGED, _sl_changed, NULL);
   evas_object_size_hint_align_set(_play_prg_sl, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(_play_prg_sl, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   elm_box_pack_end(ply_sl_box, _play_prg_sl);
   efl_weak_ref(&_play_prg_sl);
   evas_object_show(_play_prg_sl);

   _play_total_lb = elm_label_add(ply_sl_box);
   evas_object_size_hint_align_set(_play_total_lb, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(_play_total_lb, 0.1, EVAS_HINT_EXPAND);
   _media_length_update(_ply_emo, NULL);
   elm_box_pack_end(ply_sl_box, _play_total_lb);
   efl_weak_ref(&_play_total_lb);
   evas_object_show(_play_total_lb);

   Eo *ply_bts_box = elm_box_add(ply_box);
   evas_object_size_hint_weight_set(ply_bts_box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(ply_bts_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_horizontal_set(ply_bts_box, EINA_TRUE);
   elm_box_pack_end(ply_box, ply_bts_box);
   evas_object_show(ply_bts_box);

   _play_bt = button_create(ply_bts_box, NULL,
         icon_create(ply_bts_box,
            _file_playing && _file_playing->playing?"media-playback-pause":"media-playback-start", NULL),
         NULL, _media_play_pause_cb, NULL);
   elm_box_pack_end(ply_bts_box, _play_bt);
   efl_weak_ref(&_play_bt);

   return box;
}


