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

enum {
    // Error codes are dfined by Elegoo documentation (SDCPv3.0.0), we are doing best to match to our errors.
    UPLOAD_ERROR_OFFSET = -1, // offset error 	Illegal file offset value (less than 0)
    UPLOAD_ERROR_OFFSET_MISMATCH = -2, // offset not match 	File offset does not match the current file
    UPLOAD_ERROR_FILE_OPEN = -3, // file open failed 	File cannot be opened
    UPLOAD_ERROR_UNKNOWN = -4, // unknow error 	Other Unknown Errors
} upload_error_t;

void web_handle_upload(struct mg_connection *c, const mg_http_message *hm);