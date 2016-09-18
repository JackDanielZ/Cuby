#define EFL_BETA_API_SUPPORT
#include <Eo.h>
#include <Eet.h>
#include <Ecore.h>
#include <Elementary.h>

#include "common.h"

static Eet_Data_Descriptor *_music_edd = NULL;

typedef struct
{
   const char *path;
} Music_Path;

typedef struct
{
   Eina_List *paths_lst;
} Music_Cfg;

static Music_Cfg *_cfg = NULL;
static Eo *_win = NULL, *_popup = NULL, *_gl = NULL;
static Eina_Stringshare *_cfg_filename = NULL;

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
   return strdup(data);
}

static void
_genlist_refresh()
{
   static Elm_Genlist_Item_Class *itc = NULL;
   if (!itc)
     {
        itc = elm_genlist_item_class_new();
        itc->item_style = "default";
        itc->func.text_get = _text_get;
     }

   elm_genlist_clear(_gl);
   Eina_List *itr;
   const char *path;
   EINA_LIST_FOREACH(_cfg->paths_lst, itr, path)
     {
        elm_genlist_item_append(_gl, itc, path, NULL,
              ELM_GENLIST_ITEM_NONE, NULL, NULL);
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
        if (!_cfg) _cfg = calloc(1, sizeof(*_cfg));
     }
   _write_to_file(_cfg_filename);

   return EINA_TRUE;
}

Eo *
music_ui_get(Eo *parent)
{
   Eo *box = elm_box_add(parent);
   evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_horizontal_set(box, EINA_TRUE);
   evas_object_show(box);

   _gl = elm_genlist_add(parent);
   evas_object_size_hint_weight_set(_gl, 0.8, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(_gl, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(box, _gl);
   evas_object_show(_gl);

   _genlist_refresh();

   Eo *bts_box = elm_box_add(box);
   evas_object_size_hint_weight_set(bts_box, 0.2, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(bts_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   elm_box_pack_end(box, bts_box);
   evas_object_show(bts_box);

#if 0
   elm_box_pack_end(bts_box,
         button_create(bts_box, "Add memo", NULL, NULL, _memo_add_show, (void *)EINA_TRUE));
   elm_box_pack_end(bts_box,
         button_create(bts_box, "Edit memo", NULL, NULL, _memo_add_show, (void *)EINA_FALSE));
   elm_box_pack_end(bts_box, button_create(bts_box, "Del memo", NULL, NULL, _memo_del, NULL));
#endif

   return box;
}


