#define EFL_BETA_API_SUPPORT
#define EFL_EO_API_SUPPORT
#include <Eo.h>
#include <Ecore.h>
#include <Ecore_Con.h>

#include "common.h"

static const char *_base_url = "http://www.jango.com";
static void *_url_session_id_step = (void *)0;
static void *_url_song_link_step = (void *)1;
static void *_url_song_data_step = (void *)2;

static int _init_cnt = 0;

static Eina_Bool
_data_get_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event_info)
{
   Ecore_Con_Event_Url_Data *url_data = event_info;
   Ecore_Con_Url *ec_url = url_data->url_con;
   Jango_Session *s = ecore_con_url_data_get(ec_url);

   if (url_data->size > (s->data_buf_len - s->data_len))
     {
        s->data_buf_len = s->data_len + url_data->size;
        s->data_buf = realloc(s->data_buf, s->data_buf_len + 1);
     }
   memcpy(s->data_buf + s->data_len, url_data->data, url_data->size);
   s->data_len += url_data->size;
   s->data_buf[s->data_len] = '\0';

   return EINA_FALSE;
}

static Eina_Bool
_session_get_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event_info)
{
   Ecore_Con_Event_Url_Complete *url_complete = event_info;
   Ecore_Con_Url *ec_url = url_complete->url_con;
   Jango_Session *s = ecore_con_url_data_get(ec_url);
   void **step = efl_key_data_get(ec_url, "jango_step");

   if (!step || *step != _url_session_id_step) return EINA_TRUE;

   if (url_complete->status)
     {
        char *sid, *stid, *end;
        const char *hdr;
        const Eina_List *hdrs, *itr;
        sid = strstr(s->data_buf, "_jm.session_id");
        if (sid)
          {
             sid = strchr(sid, '\"') + 1;
             end = strchr(sid, '\"');
             s->session_id = calloc(end - sid + 1, 1);
             memcpy(s->session_id, sid, end - sid);
             printf("Session id: %s\n", s->session_id);
          }
        stid = strstr(s->data_buf, "_jm.station_id");
        if (stid)
          {
             stid = strchr(stid, '\"') + 1;
             end = strchr(stid, '\"');
             s->station_id = calloc(end - stid + 1, 1);
             memcpy(s->station_id, stid, end - stid);
             printf("Station id: %s\n", s->station_id);
          }
        hdrs = ecore_con_url_response_headers_get(ec_url);
        EINA_LIST_FOREACH(hdrs, itr, hdr)
          {
             char *cookie = strstr(hdr, "Set-Cookie: _jango_s");
             if (cookie)
               {
                  cookie = strchr(cookie, '_');
                  end = strchr(cookie, ';');
                  s->cookie = calloc(end - cookie + 1, 1);
                  memcpy(s->cookie, cookie, end - cookie);
               }
          }
        s->data_len = 0;
        char url[1024];
        sprintf(url, "%s/streams/info?sid=%s&stid=%s", _base_url, s->session_id, s->station_id);
        ecore_con_url_additional_header_add(ec_url, "Cookie", s->cookie);
        ecore_con_url_url_set(ec_url, url);
        efl_key_data_set(ec_url, "jango_step", &_url_song_link_step);
        ecore_con_url_get(ec_url);
     }
   return EINA_FALSE;
}

static Eina_Bool
_song_link_get_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event_info)
{
   Ecore_Con_Event_Url_Complete *url_complete = event_info;
   Ecore_Con_Url *ec_url = url_complete->url_con;
   Jango_Session *s = ecore_con_url_data_get(ec_url);
   void **step = efl_key_data_get(ec_url, "jango_step");

   if (!step || *step != _url_song_link_step) return EINA_TRUE;

   if (url_complete->status)
     {
        char *url = strstr(s->data_buf, "\"url\""), *end;
        if (!url) return EINA_TRUE;
        url = strstr(url, "http");
        end = strchr(url, '\"');
        if (end) *end = '\0';
        printf("Url %s\n", url);
        s->data_len = 0;
        Ecore_Con_Url *con_url = ecore_con_url_new(url);
        ecore_con_url_data_set(con_url, s);
        efl_key_data_set(con_url, "jango_step", &_url_song_data_step);
        ecore_con_url_get(con_url);
     }
   return EINA_FALSE;
}

static Eina_Bool
_song_data_get_cb(void *data EINA_UNUSED, int type EINA_UNUSED, void *event_info)
{
   Ecore_Con_Event_Url_Complete *url_complete = event_info;
   Ecore_Con_Url *ec_url = url_complete->url_con;
   Jango_Session *s = ecore_con_url_data_get(ec_url);
   void **step = efl_key_data_get(ec_url, "jango_step");

   if (!step || *step != _url_song_data_step) return EINA_TRUE;

   if (url_complete->status)
     {
        char name[1024];
        const char *url = ecore_con_url_url_get(ec_url);
        char *fname = strrchr(url, '/') + 1;
        sprintf(name, "%s/%.4d_%s", s->download_dir, ++s->last_song_id, fname);
        FILE *fp = fopen(name, "w");
        fwrite(s->data_buf, s->data_len, 1, fp);
        fclose(fp);
        printf("Done\n");
     }
   return EINA_FALSE;
}

Eina_Bool
jango_init()
{
   if (!_init_cnt)
     {
        ecore_event_handler_add(ECORE_CON_EVENT_URL_DATA, _data_get_cb, NULL);
        ecore_event_handler_add(ECORE_CON_EVENT_URL_COMPLETE, _session_get_cb, NULL);
        ecore_event_handler_add(ECORE_CON_EVENT_URL_COMPLETE, _song_link_get_cb, NULL);
        ecore_event_handler_add(ECORE_CON_EVENT_URL_COMPLETE, _song_data_get_cb, NULL);
     }
   _init_cnt++;
   return EINA_TRUE;
}

void
jango_shutdown()
{
   --_init_cnt;
}

Jango_Session *
jango_session_new(const char *keyword, const char *download_dir)
{
   char url[1024];
   Jango_Session *s = calloc(1, sizeof(*s));
   s->keyword = eina_stringshare_add(keyword);
   s->download_dir = eina_stringshare_add(download_dir);

   sprintf(url, "%s/music/%s", _base_url, keyword);
   s->con_url = ecore_con_url_new(url);
   ecore_con_url_additional_header_add(s->con_url, "User-Agent", "Mozilla/5.0 (X11; Linux x86_64; rv:49.0) Gecko/20100101 Firefox/49.0");
   ecore_con_url_data_set(s->con_url, s);
   efl_key_data_set(s->con_url, "jango_step", &_url_session_id_step);
   ecore_con_url_get(s->con_url);

   return s;
}
