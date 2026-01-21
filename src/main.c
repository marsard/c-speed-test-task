#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
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

void curl_test(const char *host) {
    CURL *curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, host);
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
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
