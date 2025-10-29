#ifndef APP_WEB_UPLOAD_H
#define APP_WEB_UPLOAD_H
#include "mongoose.h"

#endif //APP_WEB_UPLOAD_H

#define MAX_UPLOAD_FILENAME_LEN 255
#define TEMP_PART_UPLOAD_FILE_PATH_FORMAT "/user-resource/.upload-part-%d.tmp"
#define TEMP_UPLOAD_FILE_PATH_FORMAT "/user-resource/.upload-file-%s.tmp"

typedef struct {
    int offset;
    int total_size;
    bool check;
    char s_file_md5[33];
    char uuid[37];
    char filename[MAX_UPLOAD_FILENAME_LEN+1];
    char temp_file_path[64];
} upload_info_t;

typedef struct {
    bool offset;
    bool total_size;
    bool check;
    bool s_file_md5;
    bool uuid;
    bool file;
} upload_info_fieldset_t;

void web_handle_upload(struct mg_connection *c, const mg_http_message *hm);