#define EFL_BETA_API_SUPPORT
#include <Eo.h>
#include <Eet.h>
#include <Ecore.h>
#include <Elementary.h>

#include "common.h"

static Eet_Data_Descriptor *_music_edd = NULL;

typedef struct
{
   const char *name;
   const char *path;
   /* Not in eet */
   Eina_List *files;
   Eina_Bool expanded:1;
} Music_Path;

typedef struct
{
   Eina_List *paths_lst;
} Music_Cfg;

static Music_Cfg *_cfg = NULL;
static Eo *_win = NULL, *_popup = NULL, *_gl = NULL;
static Eina_Stringshare *_cfg_filename = NULL;
static Elm_Genlist_Item_Class *_itc = NULL;

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
_genlist_refresh()
{
   if (!_itc)
     {
        _itc = elm_genlist_item_class_new();
        _itc->item_style = "default";
        _itc->func.text_get = _text_get;
     }

   elm_genlist_clear(_gl);
   Eina_List *itr;
   Music_Path *path;
   EINA_LIST_FOREACH(_cfg->paths_lst, itr, path)
     {
        Elm_Object_Item *it = elm_genlist_item_append(_gl, _itc, path, NULL,
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
        Elm_Object_Item *it = elm_genlist_item_append(_gl, _itc, path, glit,
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

Eina_Bool
music_start(const char *filename, Eo *win)
{
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

   Eina_List *itr;
   Music_Path *path;
   EINA_LIST_FOREACH(_cfg->paths_lst, itr, path)
     {
        _files_scan(path);
     }

   return EINA_TRUE;
}

static char *
_sl_format(double val)
{
   char str[100];
   sprintf(str, "%d:%d", ((int)val) / 60, ((int)val) % 60);
   return strdup(str);
}

static void
_sl_label_free(char *str)
{
   free(str);
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

   _gl = elm_genlist_add(list_box);
   evas_object_size_hint_weight_set(_gl, 0.8, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(_gl, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(list_box, _gl);
   evas_object_show(_gl);

   evas_object_smart_callback_add(_gl, "expand,request", _expand_req, NULL);
   evas_object_smart_callback_add(_gl, "contract,request", _contract_req, NULL);
   evas_object_smart_callback_add(_gl, "expanded", _expand, NULL);
   evas_object_smart_callback_add(_gl, "contracted", _contract, NULL);

   _genlist_refresh();

   Eo *bts_box = elm_box_add(list_box);
   evas_object_size_hint_weight_set(bts_box, 0.2, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(bts_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(list_box, bts_box);
   evas_object_show(bts_box);

#if 0
   elm_box_pack_end(bts_box,
         button_create(bts_box, "Add memo", NULL, NULL, _memo_add_show, (void *)EINA_TRUE));
   elm_box_pack_end(bts_box,
         button_create(bts_box, "Edit memo", NULL, NULL, _memo_add_show, (void *)EINA_FALSE));
   elm_box_pack_end(bts_box, button_create(bts_box, "Del memo", NULL, NULL, _memo_del, NULL));
#endif

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

   Eo *ply_prg_lb = elm_label_add(ply_sl_box);
   evas_object_size_hint_align_set(ply_prg_lb, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(ply_prg_lb, 0.1, EVAS_HINT_EXPAND);
   elm_object_text_set(ply_prg_lb, "--:--");
   elm_box_pack_end(ply_sl_box, ply_prg_lb);
   evas_object_show(ply_prg_lb);

   Eo *ply_sl = elm_slider_add(ply_sl_box);
   elm_slider_indicator_format_function_set(ply_sl, _sl_format, _sl_label_free);
   elm_slider_span_size_set(ply_sl, 120);
   elm_slider_min_max_set(ply_sl, 0, 100);
   evas_object_size_hint_align_set(ply_sl, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(ply_sl, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   elm_box_pack_end(ply_sl_box, ply_sl);
   evas_object_show(ply_sl);

   Eo *ply_total_lb = elm_label_add(ply_sl_box);
   evas_object_size_hint_align_set(ply_total_lb, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(ply_total_lb, 0.1, EVAS_HINT_EXPAND);
   elm_object_text_set(ply_total_lb, "--:--");
   elm_box_pack_end(ply_sl_box, ply_total_lb);
   evas_object_show(ply_total_lb);

   Eo *ply_bts_box = elm_box_add(list_box);
   evas_object_size_hint_weight_set(ply_bts_box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(ply_bts_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_horizontal_set(ply_bts_box, EINA_TRUE);
   elm_box_pack_end(ply_box, ply_bts_box);
   evas_object_show(ply_bts_box);

   elm_box_pack_end(ply_bts_box,
         button_create(ply_bts_box, NULL,
            icon_create(ply_bts_box, "media-playback-start", NULL),
            NULL, NULL, NULL));

   return box;
}


