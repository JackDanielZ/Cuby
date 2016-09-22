#include <stdio.h>

#include <Elementary.h>

#include "common.h"

static Eina_Bool _memos_started = EINA_FALSE, _music_started = EINA_FALSE;

static Eo *_content_box = NULL;

static void
_my_win_del(void *data EINA_UNUSED, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   elm_exit(); /* exit the program's main loop that runs in elm_run() */
}

enum
{
   MEMOS_TAB,
   MUSIC_TAB
};

static void
_tab_show(void *data, Evas_Object *obj EINA_UNUSED, void *event_info EINA_UNUSED)
{
   int type = (intptr_t)data;
   Eo *content = NULL;
   switch (type)
     {
      case MEMOS_TAB:
           {
              content = memos_ui_get(_content_box);
              break;
           }
      case MUSIC_TAB:
           {
              content = music_ui_get(_content_box);
              break;
           }
      default: break;
     }
   if (content)
     {
        elm_box_clear(_content_box);
        elm_box_pack_end(_content_box, content);
     }
}

EAPI_MAIN int
elm_main()
{
   char *home_dir = getenv("HOME");
   char path[1024];
   if (!home_dir)
     {
        ERR("No home directory\n");
        return 1;
     }

   Eo *win = elm_win_util_standard_add("Cuby", "Cuby");
   evas_object_smart_callback_add(win, "delete,request", _my_win_del, NULL);
   evas_object_resize(win, 1200, 768);
   elm_win_maximized_set(win, EINA_TRUE);

   Eo *box = elm_box_add(win);
   evas_object_size_hint_align_set(box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(box, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   elm_win_resize_object_add(win, box);
   evas_object_show(box);

   Eo *bts_box = elm_box_add(box);
   evas_object_size_hint_align_set(bts_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(bts_box, EVAS_HINT_EXPAND, 0.05);
   elm_box_horizontal_set(bts_box, EINA_TRUE);
   elm_box_pack_end(box, bts_box);
   evas_object_show(bts_box);

   elm_box_pack_end(bts_box, button_create(bts_box, "Memos", NULL, NULL, _tab_show, (void *)MEMOS_TAB));
   elm_box_pack_end(bts_box, button_create(bts_box, "Music", NULL, NULL, _tab_show, (void *)MUSIC_TAB));
   elm_box_pack_end(bts_box, button_create(bts_box, "Dummy", NULL, NULL, _tab_show, (void *)-1));

   _content_box = elm_box_add(box);
   evas_object_size_hint_align_set(_content_box, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_size_hint_weight_set(_content_box, EVAS_HINT_EXPAND, 0.95);
   elm_box_pack_end(box, _content_box);
   evas_object_show(_content_box);

   sprintf(path, "%s/.cuby", home_dir);
   mkdir(path, S_IRWXU);

   sprintf(path, "%s/.cuby/memos", home_dir);
   _memos_started = memos_start(path, win);

   sprintf(path, "%s/.cuby/music", home_dir);
   _music_started = music_start(path, win);

   evas_object_show(win);

   elm_run();

   if (_music_started) music_stop();

   return 0;
}
ELM_MAIN()
