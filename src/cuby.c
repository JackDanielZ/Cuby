#include <stdio.h>

#include <Elementary.h>

#include "common.h"

static Eina_Bool _memos_started = EINA_FALSE;

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

   sprintf(path, "%s/.cuby/memos", home_dir);
   _memos_started = memos_start(path);

   return 0;
}
ELM_MAIN()
