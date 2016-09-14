#define EFL_BETA_API_SUPPORT
#include <Eo.h>
#include <Eet.h>
#include <Ecore.h>
#include <Elementary.h>

#include "common.h"

static Eet_Data_Descriptor *_memo_edd = NULL, *_memos_edd = NULL;

typedef struct
{
   int year;
   int month;
   int mday;
   int hour;
   int minute;
   int second;
   int delay_year;
   int delay_month;
   int delay_mday;
   int delay_hour;
   int delay_minute;
   int delay_second;
   const char *content;
} Memo;

typedef struct
{
   Eina_List *lst;
} Memos;

typedef enum
{
   DELAY_YEAR,
   DELAY_MONTH,
   DELAY_DAY,
   DELAY_HOUR,
   DELAY_MINUTE,
   DELAY_SECOND
} Delay_Unit;

typedef struct
{
   const char *string;
   Delay_Unit unit;
   int delay;
} Delay_Property;

static const Delay_Property _delay_props[] =
{
     {"Delay by 1 minute",   DELAY_MINUTE, 1},
     {"Delay by 5 minutes",   DELAY_MINUTE, 5},
     {"Delay by 10 minutes",  DELAY_MINUTE, 10},
     {"Delay by 60 seconds",  DELAY_SECOND, 60},
     {NULL,          DELAY_MINUTE, 0}
};

static Memos *_memos = NULL;
static Eo *_win = NULL, *_popup = NULL;
static Eina_Stringshare *_cfg_filename = NULL;

static void
_eet_load()
{
   Eet_Data_Descriptor_Class eddc;
   if (!_memo_edd)
     {
        EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Memo);
        _memo_edd = eet_data_descriptor_stream_new(&eddc);

#define CFG_ADD_BASIC(member, eet_type)\
        EET_DATA_DESCRIPTOR_ADD_BASIC\
        (_memo_edd, Memo, # member, member, eet_type)

        CFG_ADD_BASIC(year, EET_T_INT);
        CFG_ADD_BASIC(month, EET_T_INT);
        CFG_ADD_BASIC(mday, EET_T_INT);
        CFG_ADD_BASIC(hour, EET_T_INT);
        CFG_ADD_BASIC(minute, EET_T_INT);
        CFG_ADD_BASIC(content, EET_T_STRING);

#undef CFG_ADD_BASIC
     }

   if (!_memos_edd)
     {
        EET_EINA_STREAM_DATA_DESCRIPTOR_CLASS_SET(&eddc, Memos);
        _memos_edd = eet_data_descriptor_stream_new(&eddc);
        EET_DATA_DESCRIPTOR_ADD_LIST(_memos_edd, Memos, "lst", lst, _memo_edd);
     }
}

static int
_memo_sort(const void *data1, const void *data2)
{
   const Memo *m1 = data1, *m2 = data2;
   int diff;
   if ((diff = m1->year + m1->delay_year - m2->year - m2->delay_year)) return diff;
   if ((diff = m1->month + m1->delay_month - m2->month - m2->delay_month)) return diff;
   if ((diff = m1->mday + m1->delay_mday - m2->mday - m2->delay_mday)) return diff;
   if ((diff = m1->hour + m1->delay_hour - m2->hour - m2->delay_hour)) return diff;
   if ((diff = m1->minute + m1->delay_minute - m2->minute - m2->delay_minute)) return diff;
   if ((diff = m1->second + m1->delay_second - m2->second - m2->delay_second)) return diff;
   return 0;
}

static void
_memos_write_to_file(const char *filename)
{
   _memos->lst = eina_list_sort(_memos->lst, 0, _memo_sort);
   Eet_File *file = eet_open(filename, EET_FILE_MODE_WRITE);
   eet_data_write(file, _memos_edd, "entry", _memos, EINA_TRUE);
   eet_close(file);
}

enum
{
   POPUP_CLOSE,
   POPUP_DELAY
};

static int
_nb_days_in_month(int month /* 1 - 12*/, int year)
{
   switch (month)
     {
      case 1: case 3: case 5 : case 7: case 8: case 10: case 12: return 31;
      case 2: return year % 4 ? 29 : 28;
      default: return 30;
     }
}

static void
_delay_normalize(Memo *m)
{
   if (m->delay_second > 60)
     {
        m->delay_minute++;
        m->delay_second %= 60;
     }
   if (m->delay_minute > 60)
     {
        m->delay_hour++;
        m->delay_minute %= 60;
     }
   if (m->delay_hour > 24)
     {
        m->delay_mday++;
        m->delay_hour %= 24;
     }
   if (m->delay_mday > _nb_days_in_month(m->month + m->delay_month, m->year))
     {
        m->delay_month++;
        m->delay_mday %=  _nb_days_in_month(m->month + m->delay_month, m->year);
     }
   if (m->delay_month > 12)
     {
        m->delay_year++;
        m->delay_month %= 12;
     }
}

static void
_popup_close(void *data, Evas_Object *obj, void *event_info EINA_UNUSED)
{
   int choice = (intptr_t)data;
   Memo *m = efl_key_data_get(_popup, "Memo");
   switch (choice)
     {
      case POPUP_CLOSE:
           {
              _memos->lst = eina_list_remove(_memos->lst, m);
              _memos_write_to_file(_cfg_filename);
              break;
           }
      case POPUP_DELAY:
           {
              int i = 0;
              const char *text = elm_object_text_get(obj);
              while (_delay_props[i].string)
                {
                   if (!strcmp(_delay_props[i].string, text))
                     {
                        time_t t;
                        time(&t);
                        struct tm *tm = localtime(&t);

                        m->delay_year = tm->tm_year+1900 - m->year;
                        m->delay_month = tm->tm_mon+1 - m->month;
                        m->delay_mday = tm->tm_mday - m->mday;
                        m->delay_hour = tm->tm_hour - m->hour;
                        m->delay_minute = tm->tm_min - m->minute;
                        m->delay_second = tm->tm_sec - m->second;

                        switch (_delay_props[i].unit)
                          {
                           case DELAY_YEAR: m->delay_year += _delay_props[i].delay; break;
                           case DELAY_MONTH: m->delay_month += _delay_props[i].delay; break;
                           case DELAY_DAY: m->delay_mday += _delay_props[i].delay; break;
                           case DELAY_HOUR: m->delay_hour += _delay_props[i].delay; break;
                           case DELAY_MINUTE: m->delay_minute += _delay_props[i].delay; break;
                           case DELAY_SECOND: m->delay_second += _delay_props[i].delay; break;
                          }
                        _delay_normalize(m);
                        _memos_write_to_file(_cfg_filename);
                        break;
                     }
                   i++;
                }
              break;
           }
      default: break;
     }
   evas_object_del(_popup);
}

static Eina_Bool
_popup_show(Memo *m)
{
   Eo *btn, *hs;
   int i;
   if (_popup) printf("popup visible %d\n", evas_object_visible_get(_popup));
   if (!_win || _popup) return EINA_FALSE;
   _popup = elm_popup_add(_win);
   efl_key_data_set(_popup, "Memo", m);
   elm_object_text_set(_popup, m->content);
   efl_weak_ref(&_popup);

   // popup buttons
   btn = elm_button_add(_popup);
   elm_object_text_set(btn, "Dismiss");
   elm_object_part_content_set(_popup, "button1", btn);
   evas_object_smart_callback_add(btn, "clicked", _popup_close, (void *)POPUP_CLOSE);

   hs = elm_hoversel_add(_popup);
   elm_object_text_set(hs, _delay_props[0].string);
   i = 0;
   while (_delay_props[i].string)
     {
        elm_hoversel_item_add(hs, _delay_props[i].string, NULL, ELM_ICON_NONE, NULL, NULL);
        i++;
     }
   evas_object_smart_callback_add(hs, "selected", _popup_close, (void *)POPUP_DELAY);
   elm_object_part_content_set(_popup, "button2", hs);
   efl_key_data_set(btn, "delay_hoversel", hs);


   // popup show should be called after adding all the contents and the buttons
   // of popup to set the focus into popup's contents correctly.
   evas_object_show(_popup);
   return EINA_TRUE;
}

static Eina_Bool
_memo_check(void *data EINA_UNUSED)
{
   Eina_List *itr;
   Memo *m;
   time_t t;
   time(&t);
   struct tm *tm = localtime(&t);
   printf("%d/%d/%d %d:%d:%d\n",
         tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
         tm->tm_hour, tm->tm_min, tm->tm_sec);

   EINA_LIST_FOREACH(_memos->lst, itr, m)
     {
        if (m->year+m->delay_year < tm->tm_year+1900) goto consume;
        if (m->year+m->delay_year > tm->tm_year+1900) continue;

        if (m->month+m->delay_month < tm->tm_mon+1) goto consume;
        if (m->month+m->delay_month > tm->tm_mon+1) continue;

        if (m->mday+m->delay_mday < tm->tm_mday) goto consume;
        if (m->mday+m->delay_mday > tm->tm_mday) continue;

        if (m->hour+m->delay_hour < tm->tm_hour) goto consume;
        if (m->hour+m->delay_hour > tm->tm_hour) continue;

        if (m->minute+m->delay_minute < tm->tm_min) goto consume;
        if (m->minute+m->delay_minute > tm->tm_min) continue;

        if (m->second+m->delay_second < tm->tm_sec) goto consume;
        if (m->second+m->delay_second > tm->tm_sec) continue;
consume:
        _popup_show(m);
        printf("Need to consume %s\n", m->content);
     }

   return EINA_TRUE;
}

Eina_Bool
memos_start(const char *filename, Eo *win)
{
   _win = win;
   if (!_memos) _eet_load();
   _cfg_filename = eina_stringshare_add(filename);
   Eet_File *file = eet_open(_cfg_filename, EET_FILE_MODE_READ);
   if (file)
     {
        _memos = eet_data_read(file, _memos_edd, "entry");
        eet_close(file);
     }
   else
     {
        if (!_memos) _memos = calloc(1, sizeof(*_memos));

        Memo *m = calloc(1, sizeof(*m));
        m->year = 2016;
        m->month = 9;
        m->mday = 10;
        m->hour = 13;
        m->content = "Docteur pour Naomi - oreilles";
        _memos->lst = eina_list_append(_memos->lst, m);

     }
   _memos_write_to_file(_cfg_filename);

   ecore_timer_add(1.0, _memo_check, NULL);

   return EINA_TRUE;
}

static char *
_text_get(void *data, Evas_Object *obj EINA_UNUSED, const char *part EINA_UNUSED)
{
   Memo *memo = data;
   char text[1024];
   sprintf(text, "%s: %.4d/%.2d/%.2d %.2d:%.2d:00 delayed to %.4d/%.2d/%.2d %.2d:%.2d:%.2d",
         memo->content,
         memo->year, memo->month, memo->mday, memo->hour, memo->minute,
         memo->year+memo->delay_year, memo->month+memo->delay_month,
         memo->mday+memo->delay_mday, memo->hour+memo->delay_hour,
         memo->minute+memo->delay_minute, memo->delay_second);
   return strdup(text);
}

Eo *
memos_ui_get(Eo *parent)
{
   Eo *gl = elm_genlist_add(parent);
   evas_object_size_hint_weight_set(gl, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
   evas_object_size_hint_align_set(gl, EVAS_HINT_FILL, EVAS_HINT_FILL);
   evas_object_show(gl);

   Elm_Genlist_Item_Class *itc = elm_genlist_item_class_new();
   itc->item_style = "default";
   itc->func.text_get = _text_get;

   Eina_List *itr;
   Memo *memo;
   EINA_LIST_FOREACH(_memos->lst, itr, memo)
     {
        elm_genlist_item_append(gl, itc, memo, NULL,
              ELM_GENLIST_ITEM_NONE, NULL, NULL);
        printf("%s\n", memo->content);
     }

   return gl;
}

