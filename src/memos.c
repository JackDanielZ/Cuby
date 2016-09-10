#include <Eet.h>

#include "common.h"

static Eet_Data_Descriptor *_memo_edd = NULL, *_memos_edd = NULL;

typedef struct
{
   int year;
   int month;
   int mday;
   int hour;
   int minute;
   const char *content;
} Memo;

typedef struct
{
   Eina_List *lst;
} Memos;

static Memos *_memos = NULL;

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
   if (m1->year != m2->year) return m1->year - m2->year;
   if (m1->month != m2->month) return m1->month - m2->month;
   if (m1->mday != m2->mday) return m1->mday - m2->mday;
   if (m1->hour != m2->hour) return m1->hour - m2->hour;
   if (m1->minute != m2->minute) return m1->minute - m2->minute;
   return 0;
}

Eina_Bool
memos_start(const char *filename)
{
   Eina_List *itr;
   Memo *memo;
   if (!_memos) _eet_load();
   Eet_File *file = eet_open(filename, EET_FILE_MODE_READ);
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
        m->minute = 0;
        m->content = "Docteur pour Naomi - oreilles";
        _memos->lst = eina_list_append(_memos->lst, m);

        file = eet_open(filename, EET_FILE_MODE_WRITE);
        eet_data_write(file, _memos_edd, "entry", _memos, EINA_TRUE);
        eet_close(file);
     }
   _memos->lst = eina_list_sort(_memos->lst, 0, _memo_sort);

/*



   Every minute, compare the first element with the current time. If the date and hour
      correspond, create a timer and wait.

*/



   EINA_LIST_FOREACH(_memos->lst, itr, memo)
     {
        printf("%s\n", memo->content);
     }

   return EINA_TRUE;
}
