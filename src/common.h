#ifndef COMMON_H
#define COMMON_H

#include <Eina.h>
#include <Eo.h>

#define ERR(fmt, ...) fprintf(stderr, fmt"\n", ## __VA_ARGS__)

char* file_get_as_string(const char *filename);

Eina_Bool memos_start(const char *filename);
Eo *memos_ui_get(Eo *parent);

#endif

