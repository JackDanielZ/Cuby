#ifndef COMMON_H
#define COMMON_H

#define EFL_BETA_API_SUPPORT
#include <Eo.h>
#include <Eina.h>
#include <Evas.h>
#include <Ecore_Con.h>

#define ERR(fmt, ...) fprintf(stderr, fmt"\n", ## __VA_ARGS__)

typedef struct
{
   const char *keyword;
   const char *download_dir;
   Ecore_Con_Url *con_url;
   char *data_buf;
   char *session_id;
   char *station_id;
   char *cookie;
   int data_len;
   int data_buf_len;
   int last_song_id;
} Jango_Session;

typedef struct
{
   const char *filename;
   Eina_Stringshare *artist;
   Eina_Stringshare *song;
   Jango_Session *session;
   void *user_data;
   int download_progress;
   int length;
   int current_length;
} Jango_Song;

char* file_get_as_string(const char *filename);

Eina_Bool memos_start(const char *filename, Eo *win);
Eo *memos_ui_get(Eo *parent);

Eina_Bool music_start(const char *filename, Eo *win);
Eo *music_ui_get(Eo *parent);
Eina_Bool music_stop(void);

Eo *icon_create(Eo *parent, const char *path, Eo **wref);
Eo *button_create(Eo *parent, const char *text, Eo *icon, Eo **wref, Evas_Smart_Cb cb_func, void *cb_data);

typedef void (*Jango_Session_Cb)(void *data, Jango_Session *session);
typedef void (*Jango_Download_Cb)(void *data, Jango_Song *song);

Eina_Bool jango_init(void);
void jango_shutdown(void);

Jango_Session *jango_session_new(void);
void jango_download_dir_set(Jango_Session *session, const char *download_dir);
void jango_activate(Jango_Session *session, const char *keyword, Jango_Session_Cb session_cb, void *data);

void jango_fetch_next(Jango_Session *s, Jango_Download_Cb download_cb, void *data);

#endif

