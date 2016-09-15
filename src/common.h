#ifndef COMMON_H
#define COMMON_H

#define EFL_BETA_API_SUPPORT
#include <Eo.h>
#include <Eina.h>
#include <Evas.h>

#define ERR(fmt, ...) fprintf(stderr, fmt"\n", ## __VA_ARGS__)

char* file_get_as_string(const char *filename);

Eina_Bool memos_start(const char *filename, Eo *win);
Eo *memos_ui_get(Eo *parent);

Eo * button_create(Eo *parent, const char *text, Eo *icon, Eo **wref, Evas_Smart_Cb cb_func, void *cb_data);

#endif

