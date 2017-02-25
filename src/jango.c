#define EFL_BETA_API_SUPPORT
#define EFL_EO_API_SUPPORT
#include <Eo.h>
#include <Ecore.h>
#include <Ecore_Con.h>

#include "common.h"

static const char *_base_url = "http://www.jango.com";

static int _init_cnt = 0;

static void
_can_read_changed(void *data EINA_UNUSED, const Efl_Event *ev)
{
   static int max_size = 16384;
   Eina_Rw_Slice slice = { .mem = NULL };
   Eo *dialer = ev->object;

   if (efl_key_data_get(dialer, "can_read_changed")) return;
   efl_key_data_set(dialer, "can_read_changed", dialer);

   Jango_Session *s = efl_key_data_get(dialer, "Jango Session");
   if (s)
     {
        slice.mem = malloc(max_size);
        slice.len = max_size;

        while (efl_io_reader_can_read_get(dialer))
          {
             if (efl_io_reader_read(dialer, &slice)) goto ret;
             if (slice.len > (s->data_buf_len - s->data_len))
               {
                  s->data_buf_len = s->data_len + slice.len;
                  s->data_buf = realloc(s->data_buf, s->data_buf_len + 1);
               }
             memcpy(s->data_buf + s->data_len, slice.mem, slice.len);
             s->data_len += slice.len;
             s->data_buf[s->data_len] = '\0';
             slice.len = max_size;
          }
     }
   else
     {
        Jango_Song *song = efl_key_data_get(dialer, "Jango Song");
        if (song)
          {
             if (!song->length)
               {
                  Efl_Net_Http_Header *hdr;
                  Eina_Iterator *hdrs = efl_net_dialer_http_response_headers_get(dialer);
                  EINA_ITERATOR_FOREACH(hdrs, hdr)
                    {
                       if (!strcmp(hdr->key, "Content-Length"))
                         {
                            song->length = atoi(hdr->value);
                         }
                    }
                  eina_iterator_free(hdrs);
               }

             char name[1024];
             sprintf(name, "%s/%s", song->session->download_dir, song->filename);
             FILE *fp = fopen(name, "a");
             slice.mem = malloc(max_size);
             slice.len = max_size;

             while (efl_io_reader_can_read_get(dialer))
               {
                  if (efl_io_reader_read(dialer, &slice)) goto ret;
                  if (slice.len) fwrite(slice.mem, slice.len, 1, fp);
                  song->current_length += slice.len;
                  slice.len = max_size;
               }
             fclose(fp);
             song->download_progress = (song->current_length * 100) / song->length;

             Jango_Download_Cb func = efl_key_data_get(dialer, "Jango Download Cb");
             data = efl_key_data_get(dialer, "Jango Download Data");
             if (func) func(data, song);

          }
     }
ret:
   free(slice.mem);
   efl_key_data_set(dialer, "can_read_changed", NULL);
}

static Efl_Net_Dialer_Http *
_dialer_create(Eina_Bool is_get_method, const char *data, Efl_Event_Cb cb)
{
   Eo *dialer = efl_add(EFL_NET_DIALER_HTTP_CLASS, ecore_main_loop_get(),
         efl_net_dialer_http_method_set(efl_added, is_get_method?"GET":"POST"),
         efl_net_dialer_proxy_set(efl_added, NULL),
         efl_net_dialer_http_request_header_add(efl_added, "Accept-Encoding", "identity"),
         efl_net_dialer_http_request_header_add(efl_added, "User-Agent",
            "Mozilla/5.0 (X11; Linux x86_64; rv:49.0) Gecko/20100101 Firefox/49.0"),
         efl_event_callback_add(efl_added, EFL_IO_READER_EVENT_CAN_READ_CHANGED, _can_read_changed, NULL));
   if (cb)
      efl_event_callback_add(dialer, EFL_IO_READER_EVENT_EOS, cb, NULL);

   if (!is_get_method && data)
     {
        Eina_Slice slice = { .mem = data, .len = strlen(data) };
        Eo *buffer = efl_add(EFL_IO_BUFFER_CLASS, efl_loop_get(dialer),
              efl_name_set(efl_added, "post-buffer"),
              efl_io_closer_close_on_destructor_set(efl_added, EINA_TRUE),
              efl_io_closer_close_on_exec_set(efl_added, EINA_TRUE));
        efl_io_writer_write(buffer, &slice, NULL);

        efl_add(EFL_IO_COPIER_CLASS, efl_loop_get(dialer),
              efl_name_set(efl_added, "copier-buffer-dialer"),
              efl_io_copier_source_set(efl_added, buffer),
              efl_io_copier_destination_set(efl_added, dialer),
              efl_io_closer_close_on_destructor_set(efl_added, EINA_FALSE));
     }

   return dialer;
}

static void
_session_get_cb(void *data EINA_UNUSED, const Efl_Event *ev)
{
   Efl_Net_Dialer_Http *dialer = ev->object;
   Jango_Session *s = efl_key_data_get(dialer, "Jango Session");

   if (s->data_len)
     {
        char *sid, *stid, *end;
        Efl_Net_Http_Header *hdr;
        Eina_Iterator *hdrs;
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
        hdrs = efl_net_dialer_http_response_headers_get(dialer);
        EINA_ITERATOR_FOREACH(hdrs, hdr)
          {
             if (!strcmp(hdr->key, "Set-Cookie") && strstr(hdr->value, "_jango_s"))
               {
                  s->cookie = strdup(hdr->value);
               }
          }
        eina_iterator_free(hdrs);
     }
   s->data_len = 0;
   Jango_Session_Cb func = efl_key_data_get(dialer, "Jango Session Cb");
   data = efl_key_data_get(dialer, "Jango Session Data");
   efl_key_data_set(dialer, "Jango Session Cb", NULL);
   efl_key_data_set(dialer, "Jango Session Data", NULL);
   if (func) func(data, s);
}

static Jango_Song *
_song_create(Jango_Session *s, const char *artist, const char *song_name)
{
   Jango_Song *song = calloc(1, sizeof(*song));
   song->session = s;
   song->artist = eina_stringshare_add(artist);
   song->song = eina_stringshare_add(song_name);
   return song;
}

static void
_song_delete(Jango_Song *song)
{
   if (!song) return;
   eina_stringshare_del(song->artist);
   eina_stringshare_del(song->song);
   free(song);
}

static void
_song_data_end_cb(void *data, const Efl_Event *ev)
{
   Efl_Net_Dialer_Http *dialer = ev->object;

   Jango_Download_Cb func = efl_key_data_get(dialer, "Jango Download Cb");
   data = efl_key_data_get(dialer, "Jango Download Data");
   Jango_Song *song = efl_key_data_get(dialer, "Jango Song");
   efl_key_data_set(dialer, "Jango Download Cb", NULL);
   efl_key_data_set(dialer, "Jango Download Data", NULL);
   efl_key_data_set(dialer, "jango Song", NULL);
   if (func) func(data, song);
   _song_delete(song);
}

static void
_song_link_get_cb(void *data EINA_UNUSED, const Efl_Event *ev)
{
   Efl_Net_Dialer_Http *dialer = ev->object;
   Jango_Session *s = efl_key_data_get(dialer, "Jango Session");

   if (s->data_len)
     {
        char *artist, *song, *url, *end;
        Eina_Stringshare *shr_artist = NULL, *shr_song = NULL;
        printf("Data %s\n", s->data_buf);
        artist = strstr(s->data_buf, "\"artist\"");
        if (artist)
          {
             artist = strstr(artist, ":\"");
             if (artist) artist += 2;
             end = strchr(artist, '\"');
             shr_artist = eina_stringshare_add_length(artist, end - artist);
             printf("Artist: %s\n", shr_artist);
          }
        song = strstr(s->data_buf, "\"song\"");
        if (song)
          {
             song = strstr(song, ":\"");
             if (song) song += 2;
             end = strchr(song, '\"');
             shr_song = eina_stringshare_add_length(song, end - song);
             printf("Song: %s\n", shr_song);
          }

        url = strstr(s->data_buf, "\"url\"");
        if (!url) return;
        url = strstr(url, "http");
        end = strchr(url, '\"');
        if (end) *end = '\0';
        printf("Url %s\n", url);

        s->data_len = 0;

        Jango_Song *jsong = _song_create(s, shr_artist, shr_song);
        char filename[100];
        char *url_filename = strrchr(url, '/') + 1;
        sprintf(filename, "%.4d_%s", ++s->last_song_id, url_filename);
        jsong->filename = eina_stringshare_add(filename);

        Jango_Download_Cb func = efl_key_data_get(dialer, "Jango Download Cb");
        data = efl_key_data_get(dialer, "Jango Download Data");
        if (func) func(data, jsong);

        dialer = _dialer_create(EINA_TRUE, NULL, _song_data_end_cb);
        efl_key_data_set(dialer, "Jango Song", jsong);
        efl_key_data_set(dialer, "Jango Download Cb", func);
        efl_key_data_set(dialer, "Jango Download Data", data);
        efl_net_dialer_dial(dialer, url);
     }
   s->data_len = 0;
}

static void
_search_data_end_cb(void *data, const Efl_Event *ev)
{
   Efl_Net_Dialer_Http *dialer = ev->object;
   Jango_Session *s = efl_key_data_get(dialer, "Jango Session");

   if (s->data_len)
     {
        Eina_List *ret = NULL;
        char *buf = s->data_buf, *label, *end;
        while ((label = strstr(buf, "\"label\"")))
          {
             Jango_Search_Item *item = calloc(1, sizeof(*item));
             label = strstr(label, ":\"");
             if (label) label += 2;
             end = strchr(label, '\"');
             item->label = eina_stringshare_add_length(label, end - label);

             char *url = strstr(end, "\"url\"");
             if (!url) break;
             url = strstr(url, ":\"");
             if (url) url += 2;
             else break;
             end = strchr(url, '\"');
             item->url = eina_stringshare_add_length(url, end - url);

             ret = eina_list_append(ret, item);
             buf = strchr(url, '}');
          }
        Jango_Search_Ready_Cb func = efl_key_data_get(dialer, "Jango Search Ready Cb");
        data = efl_key_data_get(dialer, "Jango Search Ready Data");
        if (func) func(data, ret);
     }
   s->data_len = 0;
}

Eina_Bool
jango_init()
{
   if (!_init_cnt)
     {
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
jango_session_new(void)
{
   return calloc(1, sizeof(Jango_Session));
}

void
jango_download_dir_set(Jango_Session *s, const char *download_dir)
{
   if (s) s->download_dir = eina_stringshare_add(download_dir);
}

void
jango_activate(Jango_Session *s, const char *keyword, Jango_Session_Cb session_cb, void *data)
{
   char url[1024];
   s->keyword = eina_stringshare_add(keyword);

   sprintf(url, "%s/music/%s", _base_url, keyword);
   Efl_Net_Dialer_Http *dialer = _dialer_create(EINA_TRUE, NULL, _session_get_cb);
   efl_key_data_set(dialer, "Jango Session", s);
   efl_key_data_set(dialer, "Jango Session Cb", session_cb);
   efl_key_data_set(dialer, "Jango Session Data", data);
   efl_net_dialer_dial(dialer, url);
}

void
jango_fetch_next(Jango_Session *s, Jango_Download_Cb download_cb, void *data)
{
   char url[1024];
   sprintf(url, "%s/streams/info?sid=%s&stid=%s", _base_url, s->session_id, s->station_id);
   Efl_Net_Dialer_Http *dialer = _dialer_create(EINA_TRUE, NULL, _song_link_get_cb);
   if (s->cookie) efl_net_dialer_http_request_header_add(dialer, "Cookie", s->cookie);
   efl_key_data_set(dialer, "Jango Session", s);
   efl_key_data_set(dialer, "Jango Download Cb", download_cb);
   efl_key_data_set(dialer, "Jango Download Data", data);
   efl_net_dialer_dial(dialer, url);
}

void
jango_search(Jango_Session *s, const char *keyword, Jango_Search_Ready_Cb search_cb, void *data)
{
   char url[1024];
   sprintf(url, "%s/artists/jsearch?term=%s", _base_url, keyword);
   Efl_Net_Dialer_Http *dialer = _dialer_create(EINA_TRUE, NULL, _search_data_end_cb);
   efl_net_dialer_http_request_header_add(dialer, "Cookie", s->cookie);
   efl_key_data_set(dialer, "Jango Session", s);
   efl_key_data_set(dialer, "Jango Search Ready Cb", search_cb);
   efl_key_data_set(dialer, "Jango Search Ready Data", data);
   efl_net_dialer_dial(dialer, url);
}

void
jango_search_item_del(Jango_Search_Item *item)
{
   if (!item) return;
   eina_stringshare_del(item->label);
   eina_stringshare_del(item->url);
   free(item);
}

