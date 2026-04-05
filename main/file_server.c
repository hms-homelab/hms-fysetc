#include "file_server.h"
#include "config.h"
#include "sd_manager.h"
#include "wifi_manager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static const char *TAG = "file_srv";

#define MAX_PATH     256
#define FILE_CHUNK   4096

// URL-decode %5C to '\' then convert all backslashes to '/' in-place.
static void normalize_path(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == '%' && r[1] == '5' && (r[2] == 'C' || r[2] == 'c')) {
            *w++ = '/';
            r += 3;
        } else if (*r == '\\') {
            *w++ = '/';
            r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

// Format a directory entry as an HTML line.
static void send_entry(httpd_req_t *req, const char *name, struct stat *st,
                       bool is_dir, const char *dir_query_path)
{
    struct tm *tm = localtime(&st->st_mtime);
    if (!tm) {
        time_t now;
        time(&now);
        tm = localtime(&now);
    }

    char line[512];

    if (is_dir) {
        snprintf(line, sizeof(line),
                 "%4d-%2d-%2d   %2d:%02d:%02d         &lt;DIR&gt;   "
                 "<a href=\"dir?dir=A:%s\\%s\"> %s</a>\r\n",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_hour, tm->tm_min, tm->tm_sec,
                 dir_query_path, name, name);
    } else {
        long size_kb = (long)((st->st_size + 1023) / 1024);
        if (size_kb < 1) size_kb = 1;
        snprintf(line, sizeof(line),
                 "%4d-%2d-%2d   %2d:%02d:%02d       %5ldKB  "
                 "<a href=\"download?file=%s\\%s\"> %s</a>\r\n",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_hour, tm->tm_min, tm->tm_sec,
                 size_kb, dir_query_path, name, name);
    }
    httpd_resp_sendstr_chunk(req, line);
}

// GET /dir?dir=A:path
static esp_err_t handle_dir(httpd_req_t *req)
{
    char query[MAX_PATH * 2] = {0};
    char dir_param[MAX_PATH] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "dir", dir_param, sizeof(dir_param));
    }
    if (dir_param[0] == '\0') {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing dir param");
        return ESP_OK;
    }

    // Strip "A:" prefix if present
    char *rel_path = dir_param;
    if (rel_path[0] == 'A' && rel_path[1] == ':') {
        rel_path += 2;
    }

    normalize_path(rel_path);

    if (strstr(rel_path, "..")) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid path");
        return ESP_OK;
    }

    char abs_path[MAX_PATH + 16];
    snprintf(abs_path, sizeof(abs_path), "/sdcard/%s", rel_path);

    if (!sd_manager_take_control()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sd_busy");
        return ESP_OK;
    }
    if (!sd_manager_mount()) {
        sd_manager_release_control();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sd_mount_failed");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, "<html><body><pre>\r\n");

    DIR *dir = opendir(abs_path);
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (ent->d_name[0] == '.') continue;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
            char full[MAX_PATH + MAX_PATH + 8];
            snprintf(full, sizeof(full), "%s/%s", abs_path, ent->d_name);
#pragma GCC diagnostic pop

            struct stat st;
            if (stat(full, &st) != 0) continue;

            send_entry(req, ent->d_name, &st, S_ISDIR(st.st_mode), rel_path);
        }
        closedir(dir);
    }

    httpd_resp_sendstr_chunk(req, "</pre></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);

    sd_manager_unmount();
    sd_manager_release_control();

    ESP_LOGI(TAG, "GET /dir dir=%s", rel_path);
    return ESP_OK;
}

// GET /download?file=path
static esp_err_t handle_download(httpd_req_t *req)
{
    char query[MAX_PATH * 2] = {0};
    char file_param[MAX_PATH] = {0};

    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
        httpd_query_key_value(query, "file", file_param, sizeof(file_param)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "missing file param");
        return ESP_OK;
    }

    normalize_path(file_param);

    if (strstr(file_param, "..")) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "invalid path");
        return ESP_OK;
    }

    char abs_path[MAX_PATH + 16];
    snprintf(abs_path, sizeof(abs_path), "/sdcard/%s", file_param);

    // Check for Range header
    long offset = 0;
    char range_hdr[64] = {0};
    if (httpd_req_get_hdr_value_str(req, "Range", range_hdr, sizeof(range_hdr)) == ESP_OK) {
        char *eq = strstr(range_hdr, "bytes=");
        if (eq) {
            offset = atol(eq + 6);
            if (offset < 0) offset = 0;
        }
    }

    if (!sd_manager_take_control()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sd_busy");
        return ESP_OK;
    }
    if (!sd_manager_mount()) {
        sd_manager_release_control();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sd_mount_failed");
        return ESP_OK;
    }

    FILE *f = fopen(abs_path, "r");
    if (!f) {
        sd_manager_unmount();
        sd_manager_release_control();
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_OK;
    }

    if (offset > 0) fseek(f, offset, SEEK_SET);

    httpd_resp_set_type(req, "application/octet-stream");
    if (offset > 0) {
        httpd_resp_set_status(req, "206 Partial Content");
    }

    char *buf = malloc(FILE_CHUNK);
    if (!buf) {
        fclose(f);
        sd_manager_unmount();
        sd_manager_release_control();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_OK;
    }

    size_t total = 0, n;
    while ((n = fread(buf, 1, FILE_CHUNK, f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, (ssize_t)n) != ESP_OK) break;
        total += n;
    }
    httpd_resp_send_chunk(req, NULL, 0);

    free(buf);
    fclose(f);
    sd_manager_unmount();
    sd_manager_release_control();

    ESP_LOGI(TAG, "GET /download %s offset=%ld bytes=%zu", file_param, offset, total);
    return ESP_OK;
}

// GET /api/status
static esp_err_t handle_api_status(httpd_req_t *req)
{
    int64_t up = esp_timer_get_time() / 1000000LL;
    int secs = (int)(up % 60), mins = (int)((up / 60) % 60), hrs = (int)(up / 3600);

    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"version\":\"" FIRMWARE_VERSION "\","
             "\"state\":\"FILE_SERVER\",\"wifi\":%s,"
             "\"uptime\":\"%dh%02dm%02ds\","
             "\"build\":\"%s %s\"}",
             wifi_manager_is_connected() ? "true" : "false",
             hrs, mins, secs,
             __DATE__, __TIME__);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, buf);
}

void file_server_register_endpoints(httpd_handle_t server)
{
    httpd_uri_t uris[] = {
        { .uri = "/dir",        .method = HTTP_GET, .handler = handle_dir        },
        { .uri = "/download",   .method = HTTP_GET, .handler = handle_download   },
        { .uri = "/api/status", .method = HTTP_GET, .handler = handle_api_status },
    };
    for (int i = 0; i < 3; i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    ESP_LOGI(TAG, "Registered: /dir /download /api/status");
}
