#include "web_upload.h"
#include "hl_common.h"
#include "cJSON.h"
#include "file_manager.h"
#include <stdio.h>
#include <errno.h>
#include <math.h>

#define LOG_TAG "web_upload"
#undef LOG_LEVEL
#define LOG_LEVEL LOG_INFO
#include "log.h"

static cJSON *upload_error_response(const int error_code, const char *error_message, const char *error_field) {
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "code", "111111");
    cJSON *msg_array = cJSON_CreateArray();
    cJSON *message = cJSON_CreateObject();
    cJSON_AddNumberToObject(message, "message", error_code);
    cJSON_AddStringToObject(message, "field", "common_field");
    cJSON_AddItemToArray(msg_array, message);
    cJSON *custom_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(custom_msg, "message", error_message);
    cJSON_AddStringToObject(custom_msg, "field", error_field);
    cJSON_AddItemToArray(msg_array, custom_msg);
    cJSON_AddItemToObject(root, "messages", msg_array);
    cJSON_AddItemToObject(root, "data", cJSON_CreateObject());
    cJSON_AddBoolToObject(root, "success", false);
    return root;
}

static void upload_return_error(struct mg_connection *c, const int status_code, const int error_code,
                                const char *error_message, const char *error_field) {
    cJSON *response = upload_error_response(error_code, error_message, error_field);
    mg_http_reply(c, status_code, "", cJSON_Print(response));
    cJSON_Delete(response);
}

/*
 * Process multipart upload info from HTTP request to extract upload parameters and save uploaded chunk in temp
 * file.
 * TODO: error codes are mostly made up for now, not what Elegoo returns.
 * TODO: probably needs some more verification/error handling. We also ignore "Check" flag and always check MD5.
 *
 */
static void process_upload_info(struct mg_connection *c, const mg_http_message *hm_p, upload_info_t *upload_info_p,
                                upload_info_fieldset_t *upload_info_fs) {
    mg_http_part part;
    size_t ofs = 0;

    while ((ofs = mg_http_next_multipart(hm_p->body, ofs, &part)) > 0) {
        char name_buf[64] = {};
        memcpy(name_buf, part.name.ptr, std::min((int) part.name.len, 63));
        name_buf[part.name.len] = '\0';
        LOG_I(name_buf, "\n");
        if (strcmp(name_buf, "Offset") == 0) {
            upload_info_p->offset = atoi(std::string(part.body.ptr, part.body.len).c_str());
            upload_info_fs->offset = true;
        }
        if (strcmp(name_buf, "TotalSize") == 0) {
            upload_info_p->total_size = atoi(std::string(part.body.ptr, part.body.len).c_str());
            upload_info_fs->total_size = true;
        }
        if (strcmp(name_buf, "S-File-MD5") == 0) {
            memcpy(upload_info_p->s_file_md5, part.body.ptr, part.body.len);
            upload_info_p->s_file_md5[part.body.len] = '\0';
            for (int i = 0; upload_info_p->s_file_md5[i]; i++) {
                upload_info_p->s_file_md5[i] = tolower(upload_info_p->s_file_md5[i]);
            }
            LOG_I("S-File-MD5: %s\n", upload_info_p->s_file_md5);
            upload_info_fs->s_file_md5 = true;
        }
        if (strcmp(name_buf, "Uuid") == 0) {
            memcpy(upload_info_p->uuid, part.body.ptr, part.body.len);
            upload_info_p->uuid[part.body.len] = '\0';
            for (int i = 0; i < part.body.len; i++) {
                if (!isxdigit(upload_info_p->uuid[i]) && upload_info_p->uuid[i] != '-') {
                    LOG_E("Invalid character in UUID\n");
                    upload_return_error(c, 400, -2, "Invalid UUID format", "uuid");
                    return;
                }
            }
            LOG_I("Uuid: %s\n", upload_info_p->uuid);
            upload_info_fs->uuid = true;
        }
        if (strcmp(name_buf, "File") == 0) {
            if (part.filename.len > MAX_UPLOAD_FILENAME_LEN) {
                LOG_E("Uploaded filename too long\n");
                upload_return_error(c, 400, -3, "Filename too long", "filename");
                return;
            }
            if (part.filename.len < 1) {
                LOG_E("Uploaded filename empty\n");
                upload_return_error(c, 400, -4, "Filename cannot be empty", "filename");
                return;
            }
            memcpy(upload_info_p->filename, part.filename.ptr, part.filename.len);
            upload_info_p->filename[part.filename.len] = '\0';
            snprintf(upload_info_p->temp_file_path, sizeof(upload_info_p->temp_file_path),
                     TEMP_PART_UPLOAD_FILE_PATH_FORMAT, rand());
            FILE *fp = fopen(upload_info_p->temp_file_path, "wb");
            if (fp == NULL) {
                LOG_E("Failed to open temporary file for writing\n");
                upload_return_error(c, 500, -3, "Internal server error", "file");
                return;
            }
            size_t written = fwrite(part.body.ptr, 1, part.body.len, fp);
            fclose(fp);
            if (written != part.body.len) {
                LOG_E("Failed to write complete file chunk to temporary file\n");
                upload_return_error(c, 500, -3, "Internal server error", "file");
                return;
            }
            upload_info_fs->file = true;
            LOG_I("Receiving file chunk of size %lu at offset %d\n", part.body.len, upload_info_p->offset);
        }
    }
}

/*
 * Process uploaded chunk: concat with previous chunks, check for completion, verify MD5, move to final location.
 * TODO: error codes are mostly made up for now, not what Elegoo returns.
 * TODO: same as process_upload_info, some more error handling would be nice.
 */
static void process_upload_chunk(struct mg_connection *c, upload_info_t upload_info) {
    char temp_file_path[64];

    if (upload_info.offset == 0) {
        LOG_I("Starting new upload for file %s of total size %d\n", upload_info.filename, upload_info.total_size);
        char final_temp_file_path[128];
        snprintf(final_temp_file_path, sizeof(final_temp_file_path), TEMP_UPLOAD_FILE_PATH_FORMAT, upload_info.uuid);
        unlink(final_temp_file_path);
        if (rename(upload_info.temp_file_path, final_temp_file_path)) {
            LOG_E("Failed to rename temporary upload file to final temp file: %s\n", strerror(errno));
            unlink(upload_info.temp_file_path);
            unlink(final_temp_file_path);
            upload_return_error(c, 500, -3, "Internal server error", "file");
            return;
        }
    } else {
        // use existing temp uuid file and append new chunk (fail if doesn't exist, or existing file size != offset)
        char final_temp_file_path[128];
        snprintf(final_temp_file_path, sizeof(final_temp_file_path), TEMP_UPLOAD_FILE_PATH_FORMAT, upload_info.uuid);
        FILE *fp = fopen(final_temp_file_path, "ab");
        if (fp == NULL) {
            LOG_E("Failed to open existing upload file for appending\n");
            unlink(upload_info.temp_file_path);
            upload_return_error(c, 400, -5, "No existing upload found", "file");
            return;
        }
        fseek(fp, 0, SEEK_END);
        long existing_size = ftell(fp);
        if (existing_size != upload_info.offset) {
            LOG_E("Existing upload file size %ld does not match expected offset %d\n", existing_size,
                  upload_info.offset);
            fclose(fp);
            unlink(upload_info.temp_file_path);
            upload_return_error(c, 400, -6, "Offset mismatch", "offset");
            return;
        }
        // read from temp part file and append to final temp file
        FILE *part_fp = fopen(upload_info.temp_file_path, "rb");
        if (part_fp == NULL) {
            LOG_E("Failed to open temporary part file for reading\n");
            fclose(fp);
            unlink(upload_info.temp_file_path);
            upload_return_error(c, 500, -3, "Internal server error", "file");
            return;
        }
        fseek(part_fp, 0, SEEK_END);
        long part_size = ftell(part_fp);
        fseek(part_fp, 0, SEEK_SET);
        char *buffer = (char *) malloc(part_size);
        fread(buffer, 1, part_size, part_fp);
        size_t written = fwrite(buffer, 1, part_size, fp);
        free(buffer);
        fclose(part_fp);
        fclose(fp);
        if (written != part_size) {
            LOG_E("Failed to append complete file chunk to existing upload file\n");
            unlink(upload_info.temp_file_path);
            upload_return_error(c, 500, -3, "Internal server error", "file");
            return;
        }
        unlink(upload_info.temp_file_path);
    }
    // check if upload is complete (total_size == file size and md5 sum checks out)
    char final_temp_file_path[128];
    snprintf(final_temp_file_path, sizeof(final_temp_file_path), TEMP_UPLOAD_FILE_PATH_FORMAT, upload_info.uuid);
    FILE *fp = fopen(final_temp_file_path, "rb");
    if (fp == NULL) {
        LOG_E("Failed to open existing upload file for size check\n");
        unlink(final_temp_file_path);
        upload_return_error(c, 500, -3, "Internal server error", "file");
        return;
    }
    fseek(fp, 0, SEEK_END);
    long existing_size = ftell(fp);
    fclose(fp);
    if (existing_size == upload_info.total_size) {
        LOG_I("Upload complete for file %s, size %ld bytes\n", upload_info.filename, existing_size);
        uint8_t md5_sum[16];
        hl_md5(final_temp_file_path, md5_sum);
        char md5_sum_hex[33] = {};
        for (int i = 0; i < 16; i++) {
            sprintf(&md5_sum_hex[i * 2], "%02x", md5_sum[i]);
        }
        if (strcmp(md5_sum_hex, upload_info.s_file_md5) != 0) {
            LOG_E("MD5 checksum mismatch: calculated %s, expected %s\n", md5_sum_hex, upload_info.s_file_md5);
            unlink(final_temp_file_path);
            upload_return_error(c, 400, -7, "MD5 checksum mismatch", "s_file_md5");
            return;
        }
        // move file to /user-resource/(filename) and call FileManager::GetInstance()->AddFile(filename);
        char final_file_path[PATH_MAX];
        snprintf(final_file_path, sizeof(final_file_path), "/user-resource/%s", upload_info.filename);
        if (rename(final_temp_file_path, final_file_path) != 0) {
            LOG_E("Failed to move uploaded file to final location: %s\n", strerror(errno));
            unlink(final_temp_file_path);
            upload_return_error(c, 500, -3, "Internal server error", "file");
            return;
        }
        if (FileManager::GetInstance()->AddFile(final_file_path) == 0) {
            LOG_I("File %s added to FileManager\n", upload_info.filename);
            mg_http_reply(c, 200, "", "{\"code\":\"000000\",\"messages\":null,\"data\":{},\"success\":true}");
        } else {
            LOG_E("Failed to add file %s to FileManager\n", upload_info.filename);
            unlink(final_temp_file_path);
            upload_return_error(c, 500, -8, "Failed to add file to FileManager", "file");
            return;
        }
    } else {
        LOG_I("Upload in progress for file %s, current size %ld bytes, total size %d bytes\n", upload_info.filename,
              existing_size, upload_info.total_size);
        mg_http_reply(c, 200, "", "{\"code\":\"000000\",\"messages\":null,\"data\":{},\"success\":true}");
    }
}

void web_handle_upload(struct mg_connection *c, const mg_http_message *hm) {
    upload_info_t upload_info = {};
    upload_info_fieldset_t upload_info_fieldset = {false, false, false, false, false, false};
    process_upload_info(c, hm, &upload_info, &upload_info_fieldset);

    if (upload_info_fieldset.offset &&
        upload_info_fieldset.total_size &&
        upload_info_fieldset.s_file_md5 &&
        upload_info_fieldset.uuid &&
        upload_info_fieldset.file) {
        process_upload_chunk(c, upload_info);
    } else {
        LOG_E("Missing required upload fields\n");
        if (strlen(upload_info.temp_file_path) > 0) {
            unlink(upload_info.temp_file_path);
        }
        upload_return_error(c, 400, -2, "Missing required upload fields", "filename");
    }
}
