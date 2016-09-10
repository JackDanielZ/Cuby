#include "common.h"

char *
line_get(const char *buffer)
{
   char *ret = NULL;
   char *nl = strchr(buffer, '\n');
   int len = 0;
   if (nl) len = nl - buffer;
   else len = strlen(buffer);

   if (len)
     {
        ret = malloc(len + 1);
        memcpy(ret, buffer, len);
        ret[len] = '\0';
     }
   return ret;
}

char*
file_get_as_string(const char *filename)
{
   char *file_data = NULL;
   int file_size;
   FILE *fp = fopen(filename, "r");
   if (!fp)
     {
        ERR("Can not open file: \"%s\".", filename);
        return NULL;
     }

   fseek(fp, 0, SEEK_END);
   file_size = ftell(fp);
   if (file_size == -1)
     {
        fclose(fp);
        ERR("Can not ftell file: \"%s\".", filename);
        return NULL;
     }
   rewind(fp);
   file_data = (char *) calloc(1, file_size + 1);
   if (!file_data)
     {
        fclose(fp);
        ERR("Calloc failed");
        return NULL;
     }
   int res = fread(file_data, file_size, 1, fp);
   fclose(fp);
   if (!res)
     {
        free(file_data);
        file_data = NULL;
        ERR("fread failed");
     }
   return file_data;
}

