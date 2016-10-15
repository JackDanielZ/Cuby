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
} Music_Library;

typedef struct
{
   const char *path;
   Music_Library *mlib;
} Playlist_File;

typedef struct
{
   const char *name;
   Eina_List *files; /* List of Playlist_File */
} Playlist;

typedef struct
{
   Eina_List *libraries; /* List of Music_Library */
   Eina_List *playlists; /* List of Playlist */
   const char *current_playlist;
} Music_Cfg;

static Music_Cfg *_cfg = NULL;

static Eo *_win = NULL, *_libraries_gl = NULL, *_popup = NULL;
static Eina_Stringshare *_cfg_filename = NULL;
static Elm_Genlist_Item_Class *_libraries_itc = NULL, *_playlist_itc = NULL;

static Eo *_playlist_gl = NULL;
static Playlist *_current_playlist = NULL;

static Eo *_ply_emo = NULL, *_play_total_lb = NULL, *_play_prg_lb = NULL, *_play_prg_sl = NULL;
static Eo *_play_bt = NULL;
static Music_Library *_file_playing = NULL;

static void
_eet_load()
{
   Eet_Data_Descriptor_Class eddc;
   if (!_music_edd)
     {
        /* Music Library */
        EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Music_Library);
        Eet_Data_Descriptor *music_library_edd = eet_data_descriptor_stream_new(&eddc);
        EET_DATA_DESCRIPTOR_ADD_BASIC(music_library_edd, Music_Library, "name", name, EET_T_STRING);
        EET_DATA_DESCRIPTOR_ADD_BASIC(music_library_edd, Music_Library, "path", path, EET_T_STRING);

        /* Playlist File */
        EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Playlist_File);
        Eet_Data_Descriptor *playlist_file_edd = eet_data_descriptor_stream_new(&eddc);
        EET_DATA_DESCRIPTOR_ADD_BASIC(playlist_file_edd, Playlist_File, "path", path, EET_T_STRING);

        /* Playlist */
        EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Playlist);
        Eet_Data_Descriptor *playlist_edd = eet_data_descriptor_stream_new(&eddc);
        EET_DATA_DESCRIPTOR_ADD_BASIC(playlist_edd, Playlist, "name", name, EET_T_STRING);
        EET_DATA_DESCRIPTOR_ADD_LIST(playlist_edd, Playlist, "files", files, playlist_file_edd);

        /* Music Cfg */
        EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Music_Cfg);
        _music_edd = eet_data_descriptor_stream_new(&eddc);
        EET_DATA_DESCRIPTOR_ADD_LIST(_music_edd, Music_Cfg, "libraries", libraries, music_library_edd);
        EET_DATA_DESCRIPTOR_ADD_LIST(_music_edd, Music_Cfg, "playlists", playlists, playlist_edd);
        EET_DATA_DESCRIPTOR_ADD_BASIC(_music_edd, Music_Cfg, "current_playlist", current_playlist, EET_T_STRING);
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
_libraries_text_get(void *data, Evas_Object *obj EINA_UNUSED, const char *part EINA_UNUSED)
{
   Music_Library *mlib = data;
   return strdup(mlib->name?mlib->name:mlib->path);
}

static void
_libraries_genlist_refresh()
{
   if (!_libraries_itc)
     {
        _libraries_itc = elm_genlist_item_class_new();
        _libraries_itc->item_style = "default";
        _libraries_itc->func.text_get = _libraries_text_get;
     }

   elm_genlist_clear(_libraries_gl);
   Eina_List *itr;
   Music_Library *mlib;
   EINA_LIST_FOREACH(_cfg->libraries, itr, mlib)
     {
        Elm_Object_Item *it = elm_genlist_item_append(_libraries_gl, _libraries_itc, mlib, NULL,
              mlib->files?ELM_GENLIST_ITEM_TREE:ELM_GENLIST_ITEM_NONE,
              NULL, NULL);
        elm_genlist_item_expanded_set(it, mlib->expanded);
     }
}

static void
_expand(void *data EINA_UNUSED, Evas_Object *cont EINA_UNUSED, void *event_info)
{
   Elm_Object_Item *glit = event_info;
   Music_Library *mlib = elm_object_item_data_get(glit);
   Eina_List *itr;
   mlib->expanded = EINA_TRUE;
   EINA_LIST_FOREACH(mlib->files, itr, mlib)
     {
        Elm_Object_Item *it = elm_genlist_item_append(_libraries_gl, _libraries_itc, mlib, glit,
              mlib->files?ELM_GENLIST_ITEM_TREE:ELM_GENLIST_ITEM_NONE,
              NULL, NULL);
        elm_genlist_item_expanded_set(it, mlib->expanded);
     }
}

static void
_contract(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info)
{
   Elm_Object_Item *glit = event_info;
   Music_Library *mlib = elm_object_item_data_get(glit);
   elm_genlist_item_subitems_clear(glit);
   mlib->expanded = EINA_FALSE;
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
_files_scan(Music_Library *mlib)
{
   Eina_List *lst = ecore_file_ls(mlib->path);
   char *name;
   EINA_LIST_FREE(lst, name)
     {
        char full_path[1024];
        Music_Library *fpath = calloc(1, sizeof(*fpath));
        sprintf(full_path, "%s/%s", mlib->path, name);
        fpath->path = eina_stringshare_add(full_path);
        fpath->name = eina_stringshare_add(name);
        if (ecore_file_is_dir(full_path)) _files_scan(fpath);
        mlib->files = eina_list_append(mlib->files, fpath);
        free(name);
     }
}

static void
_files_list_clear(Music_Library *mlib)
{
   Music_Library *fpath;
   EINA_LIST_FREE(mlib->files, fpath)
     {
        _files_list_clear(fpath);
        free(fpath);
     }
}

static void
_music_library_free(Music_Library *mlib)
{
   Music_Library *fmlib;
   if (!mlib) return;
   EINA_LIST_FREE(mlib->files, fmlib)
     {
        _music_library_free(fmlib);
     }
   free(mlib);
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
   Elm_Object_Item *sel = elm_genlist_selected_item_get(_libraries_gl);
   Music_Library *mlib = sel ? elm_object_item_data_get(sel) : NULL;

   /* The selected path is different of the played path */
   if (mlib && mlib != _file_playing)
     {
        emotion_object_play_set(_ply_emo, EINA_FALSE);
        if (_file_playing) _file_playing->playing = EINA_FALSE;
        _file_playing = mlib;
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
   Music_Library *m = efl_key_data_get(obj, "mlib");
   Eo *entry = efl_key_data_get(obj, "entry");
   Eo *fs_entry = efl_key_data_get(obj, "fs_entry");

   if (!m)
     {
        m = calloc(1, sizeof(*m));
        _cfg->libraries = eina_list_append(_cfg->libraries, m);
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
   _libraries_genlist_refresh();
   evas_object_del(_popup);
}

static void
_dir_add_show(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Eina_Bool is_add = (Eina_Bool)(intptr_t)data;

   Elm_Object_Item *sel = elm_genlist_selected_item_get(_libraries_gl);
   Music_Library *m = !is_add ? elm_object_item_data_get(sel) : NULL;
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
        efl_key_data_set(add_bt, "mlib", m);
     }
   evas_object_show(_popup);
}

static void
_dir_del(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Elm_Object_Item *sel = elm_genlist_selected_item_get(_libraries_gl);
   if (!sel) return;
   Music_Library *m = elm_object_item_data_get(sel);
   _files_list_clear(m);
   _cfg->libraries = eina_list_remove(_cfg->libraries, m);
   _libraries_genlist_refresh();
   _write_to_file(_cfg_filename);
}

static char *
_playlist_text_get(void *data, Evas_Object *obj EINA_UNUSED, const char *part EINA_UNUSED)
{
   Playlist_File *plp = data;
   return strdup(plp->path);
}

static void
_playlist_genlist_refresh()
{
   Eina_List *itr;
   Playlist_File *plp;
   if (!_playlist_itc)
     {
        _playlist_itc = elm_genlist_item_class_new();
        _playlist_itc->item_style = "default";
        _playlist_itc->func.text_get = _playlist_text_get;
     }

   elm_genlist_clear(_playlist_gl);
   EINA_LIST_FOREACH(_current_playlist?_current_playlist->files:NULL, itr, plp)
     {
        elm_genlist_item_append(_playlist_gl, _playlist_itc, plp, NULL,
              ELM_GENLIST_ITEM_NONE, NULL, NULL);
     }
}

static void
_selected_path_transfer(void *data EINA_UNUSED, Eo *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   Elm_Object_Item *sel = elm_genlist_selected_item_get(_libraries_gl);
   if (!sel) return;
   Music_Library *m = elm_object_item_data_get(sel);
   if (!m) return;

   Playlist_File *plp = calloc(1, sizeof(*plp));
   plp->path = strdup(m->path);
   _current_playlist->files = eina_list_append(_current_playlist->files, plp);
   _playlist_genlist_refresh();
   _write_to_file(_cfg_filename);
}

Eina_Bool
music_start(const char *filename, Eo *win)
{
   Playlist *pl;
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
        Music_Library *mlib = calloc(1, sizeof(*mlib));
        sprintf(dflt_path, "%s/Music", home_dir);
        mlib->name = eina_stringshare_add("Music");
        mlib->path = eina_stringshare_add(dflt_path);
        _cfg->libraries = eina_list_append(_cfg->libraries, mlib);
        /* Default playlist */
        pl = calloc(1, sizeof(*pl));
        pl->name = strdup("Unnamed");
        _cfg->playlists = eina_list_append(_cfg->playlists, pl);
        _cfg->current_playlist = strdup(pl->name);
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
   Music_Library *mlib;
   EINA_LIST_FOREACH(_cfg->libraries, itr, mlib)
     {
        _files_scan(mlib);
     }

   EINA_LIST_FOREACH(_cfg->playlists, itr, pl)
     {
        if (!strcmp(pl->name, _cfg->current_playlist))
          {
             _current_playlist = pl;
          }
     }
   return EINA_TRUE;
}

Eina_Bool
music_stop(void)
{
   if (_cfg)
     {
        Music_Library *mlib;
        EINA_LIST_FREE(_cfg->libraries, mlib)
          {
             _music_library_free(mlib);
          }
     }
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

   /* Paths and playlist box */
   Eo *list_box = elm_box_add(box);
   evas_object_size_hint_weight_set(list_box, EVAS_HINT_EXPAND, 0.9);
   evas_object_size_hint_align_set(list_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_horizontal_set(list_box, EINA_TRUE);
   elm_box_pack_end(box, list_box);
   evas_object_show(list_box);

   /* Paths box */
   Eo *libraries_box = elm_box_add(list_box);
   evas_object_size_hint_weight_set(libraries_box, 0.4, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(libraries_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(list_box, libraries_box);
   evas_object_show(libraries_box);

   /* Add/del/edit dirs buttons box */
   Eo *bts_box = elm_box_add(libraries_box);
   elm_box_horizontal_set(bts_box, EINA_TRUE);
   evas_object_size_hint_weight_set(bts_box, EVAS_HINT_EXPAND, 0.05);
   evas_object_size_hint_align_set(bts_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(libraries_box, bts_box);
   evas_object_show(bts_box);

   /* Add/del/edit dirs buttons */
   elm_box_pack_end(bts_box,
         button_create(bts_box, "Add directory", NULL, NULL, _dir_add_show, (void *)EINA_TRUE));
   elm_box_pack_end(bts_box,
         button_create(bts_box, "Edit directory", NULL, NULL, _dir_add_show, (void *)EINA_FALSE));
   elm_box_pack_end(bts_box, button_create(bts_box, "Delete directory", NULL, NULL, _dir_del, NULL));

   /* Paths genlist */
   _libraries_gl = elm_genlist_add(libraries_box);
   evas_object_size_hint_weight_set(_libraries_gl, EVAS_HINT_EXPAND, 0.95);
   evas_object_size_hint_align_set(_libraries_gl, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(libraries_box, _libraries_gl);
   evas_object_show(_libraries_gl);

   evas_object_smart_callback_add(_libraries_gl, "expand,request", _expand_req, NULL);
   evas_object_smart_callback_add(_libraries_gl, "contract,request", _contract_req, NULL);
   evas_object_smart_callback_add(_libraries_gl, "expanded", _expand, NULL);
   evas_object_smart_callback_add(_libraries_gl, "contracted", _contract, NULL);

   _libraries_genlist_refresh();

   /* Transfer box */
   Eo *transfer_box = elm_box_add(list_box);
   evas_object_size_hint_weight_set(transfer_box, 0.05, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(transfer_box, EVAS_HINT_FILL, 0.5);
   elm_box_pack_end(list_box, transfer_box);
   evas_object_show(transfer_box);

   /* Transfer to current playlist button */
   elm_box_pack_end(transfer_box,
         button_create(transfer_box, NULL,
            icon_create(transfer_box, "go-next", NULL),
            NULL, _selected_path_transfer, NULL));

   /* Playlist box */
   Eo *playlist_box = elm_box_add(list_box);
   evas_object_size_hint_weight_set(playlist_box, 0.4, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(playlist_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(list_box, playlist_box);
   evas_object_show(playlist_box);

   /* Playlist buttons box - load(hoversel of saved playlists?)/up/down/del/save */
   Eo *playlist_bts_box = elm_box_add(playlist_box);
   elm_box_horizontal_set(playlist_bts_box, EINA_TRUE);
   evas_object_size_hint_weight_set(playlist_bts_box, EVAS_HINT_EXPAND, 0.05);
   evas_object_size_hint_align_set(playlist_bts_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(playlist_box, playlist_bts_box);
   evas_object_show(playlist_bts_box);

   /* Playlist genlist */
   _playlist_gl = elm_genlist_add(playlist_box);
   evas_object_size_hint_weight_set(_playlist_gl, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(_playlist_gl, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(playlist_box, _playlist_gl);
   evas_object_show(_playlist_gl);

   _playlist_genlist_refresh();

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


