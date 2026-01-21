#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include "cJSON.h"

cJSON *read_json_file(const char *filename) {
    FILE *stream = fopen(filename, "r");
    if (!stream) {
        fprintf(stderr, "error opening file: %s", filename);
        exit(EXIT_FAILURE);
    }

    /* Get file size */
    fseek(stream, 0, SEEK_END);
    long size = ftell(stream);
    fseek(stream, 0, SEEK_SET);

    /* Read into buffer */
    char *buffer = malloc(size + 1);
    fread(buffer, 1, size, stream);
    buffer[size] = '\0';
    fclose(stream);

    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        fprintf(stderr, "Parse error: %.100s\n", cJSON_GetErrorPtr());
        free(buffer);
        exit(EXIT_FAILURE);
    }
    free(buffer);

    return json;
}

struct transfer_data {
    size_t total_bytes;  /* Accumulated bytes for download or upload */
};

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    (void)contents;
    struct transfer_data *data = (struct transfer_data *)userp;
    size_t realsize = size * nmemb;
    data->total_bytes += realsize;
    return realsize;
}

static int xferinfo_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow) {
    (void)clientp;
    (void)ultotal;
    (void)ulnow;

    /* Show progress every 1MB */
    static curl_off_t last_bytes_shown = 0;
    curl_off_t one_mb = 1024 * 1024;

    if (dlnow >= last_bytes_shown + one_mb) {
        if (dltotal > 0) {
            /* Server sent Content-Length, show percentage */
            double percent = (dlnow * 100.0) / dltotal;
            double mb_current = dlnow / (1024.0 * 1024.0);
            double mb_total = dltotal / (1024.0 * 1024.0);
            printf("\rDownload progress: %.2f / %.2f MB (%.1f%%)...",
                   mb_current, mb_total, percent);
        } else {
            /* No Content-Length, just show downloaded amount */
            double mb_current = dlnow / (1024.0 * 1024.0);
            printf("\rDownload progress: %.2f MB downloaded...", mb_current);
        }
        fflush(stdout);
        last_bytes_shown = dlnow;
    }

    return 0;
}

void curl_test(const char *host) {
    CURL *curl = curl_easy_init();
    if (curl) {
        char url[256];
        strcpy(url, "http://");
        strcat(url, host);
        strcat(url, "/speedtest/random4000x4000.jpg");

        struct transfer_data data;
        data.total_bytes = 0;

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &data);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");

        printf("Testing download speed from %s...\n", host);
        CURLcode res = curl_easy_perform(curl);
        printf("\n");

        if (res == CURLE_OK) {
            long response_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
            double total_time;
            curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_time);
            if (response_code == 200 && total_time > 0 && data.total_bytes > 0) {
                double speed_bps = (data.total_bytes * 8.0) / total_time;
                double speed_mbps = speed_bps / 1000000.0;
                double mb_downloaded = data.total_bytes / (1024.0 * 1024.0);
                printf("Downloaded %.2f MB in %.2f seconds\n", mb_downloaded, total_time);
                printf("Download speed: %.2f Mbps\n", speed_mbps);
            } else {
                if (response_code != 200) {
                    printf("Warning: Server returned error code %ld\n", response_code);
                } else {
                    printf("Warning: No data downloaded or time is zero\n");
                }
            }
        } else {
            fprintf(stderr, "Download failed: %s\n", curl_easy_strerror(res));
        }
        curl_easy_cleanup(curl);
    }
}

int main(int argc, char *argv[]) {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    cJSON *json = read_json_file("speedtest_server_list.json");
    cJSON *first = cJSON_GetArrayItem(json, 0);
    cJSON *host_item = cJSON_GetObjectItem(first, "host");
    if (host_item && cJSON_IsString(host_item)) {
        const char *host = cJSON_GetStringValue(host_item);
        printf("Testing host: %s\n", host);
        curl_test(host);
    }

    /* Print json structure */
    /*
    if (cJSON_IsObject(json)) {
        printf("json is an object.\n");
    } else if (cJSON_IsArray(json)) {
        printf("json is an array with %d items\n", cJSON_GetArraySize(json));
        if (cJSON_GetArraySize(json) > 0) {
            cJSON *first = cJSON_GetArrayItem(json, 0);
            if (cJSON_IsObject(first)) {
                printf("first item is an object with fields:\n");
                cJSON *item = first->child;
                while (item) {
                    printf("- %s\n", item->string);
                    item = item->next;
                }
            }
        }
    }*/


    int option;
    while ((option = getopt(argc, argv, "du:")) != -1) {
        switch (option) {
            case 'd':
                printf("download option\n");
                break;
            case 'u':
                printf("upload option\n");
                printf("arg: %s \n", optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s [du] \n", argv[0]);
        }
    }

    /* Cleanup */
    cJSON_Delete(json);
    curl_global_cleanup();

    return EXIT_SUCCESS;
}
