#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <curl/curl.h>
#include "cJSON.h"

/* Constants */
#define SPEEDTEST_TIMEOUT_SEC 15
#define UPLOAD_SIZE_MB 25
#define LOCATION_API_URL "http://ip-api.com/json/"
#define LOCATION_API_TIMEOUT_SEC 10
#define DOWNLOAD_PATH "/speedtest/random4000x4000.jpg"
#define UPLOAD_PATH "/speedtest/upload.php"

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
    char *upload_buffer; /* Buffer for upload data */
    size_t upload_size;  /* Total size of upload buffer */
    size_t upload_sent;  /* Bytes already sent */
};

struct response_data {
    char *buffer;
    size_t size;
};

struct progress_data {
    curl_off_t last_bytes_shown;
    int is_upload;
};

static int test_server_reachable(const char *host) {
    CURL *curl = curl_easy_init();
    int reachable = 0;

    if (!curl) {
        return 0;
    }

    char url[256];
    strcpy(url, "http://");
    strcat(url, host);
    strcat(url, "/");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_NOBODY, 1L); /* HEAD request */
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");

    CURLcode res = curl_easy_perform(curl);
    if (res == CURLE_OK) {
        long response_code;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        if (response_code >= 200 && response_code < 500) {
            reachable = 1;
        }
    }

    curl_easy_cleanup(curl);
    return reachable;
}

/* Find best server by location */
static cJSON *find_best_server(cJSON *json_array, const char *user_country, const char *user_city) {
    if (!json_array || !cJSON_IsArray(json_array)) {
        return NULL;
    }

    int count = cJSON_GetArraySize(json_array);
    int i;
    cJSON *server;
    const char *host;
    const char *country;
    const char *city;

    /* Priority 1: test all city+country matches */
    if (user_city && user_country) {
        for (i = 0; i < count; i++) {
            server = cJSON_GetArrayItem(json_array, i);
            if (!server || !cJSON_IsObject(server)) {
                continue;
            }

            host = cJSON_GetStringValue(cJSON_GetObjectItem(server, "host"));
            country = cJSON_GetStringValue(cJSON_GetObjectItem(server, "country"));
            city = cJSON_GetStringValue(cJSON_GetObjectItem(server, "city"));

            if (!host || !city || !country) {
                continue;
            }

            if (strcmp(city, user_city) == 0 && strcmp(country, user_country) == 0) {
                if (test_server_reachable(host)) {
                    return server;
                }
            }
        }
    }

    /* Priority 2: test all country matches (only if no city+country worked) */
    if (user_country) {
        for (i = 0; i < count; i++) {
            server = cJSON_GetArrayItem(json_array, i);
            if (!server || !cJSON_IsObject(server)) {
                continue;
            }

            host = cJSON_GetStringValue(cJSON_GetObjectItem(server, "host"));
            country = cJSON_GetStringValue(cJSON_GetObjectItem(server, "country"));
            city = cJSON_GetStringValue(cJSON_GetObjectItem(server, "city"));

            if (!host || !country || !city) {
                continue;
            }

            if (strcmp(country, user_country) != 0) {
                continue;
            }

            /* Skip if it's a city+country match (already tested in priority 1) */
            if (user_city && strcmp(city, user_city) == 0) {
                continue;
            }

            if (test_server_reachable(host)) {
                return server;
            }
        }
    }

    /* Priority 3: test any server (only if no country match worked) */
    for (i = 0; i < count; i++) {
        server = cJSON_GetArrayItem(json_array, i);
        if (!server || !cJSON_IsObject(server)) {
            continue;
        }

        host = cJSON_GetStringValue(cJSON_GetObjectItem(server, "host"));
        country = cJSON_GetStringValue(cJSON_GetObjectItem(server, "country"));
        city = cJSON_GetStringValue(cJSON_GetObjectItem(server, "city"));

        if (!host || !country || !city) {
            continue;
        }

        /* Skip if already tested as city+country or country match */
        if (user_country && strcmp(country, user_country) == 0) {
            continue;
        }

        if (test_server_reachable(host)) {
            return server;
        }
    }

    return NULL;
}

static size_t download_write_callback(char *buffer, size_t size, size_t nitems, void *outstream) {
    (void)buffer;
    struct transfer_data *data = (struct transfer_data *)outstream;
    size_t realsize = size * nitems;
    data->total_bytes += realsize;
    return realsize;
}

static size_t api_response_callback(char *buffer, size_t size, size_t nitems, void *outstream) {
    struct response_data *data = (struct response_data *)outstream;
    size_t realsize = size * nitems;

    data->buffer = realloc(data->buffer, data->size + realsize + 1);
    if (data->buffer) {
        memcpy(&(data->buffer[data->size]), buffer, realsize);
        data->size += realsize;
        data->buffer[data->size] = '\0';
    }

    return realsize;
}

static size_t upload_read_callback(char *buffer, size_t size, size_t nitems, void *instream) {
    struct transfer_data *data = (struct transfer_data *)instream;
    size_t max_bytes = size * nitems;
    size_t remaining = data->upload_size - data->upload_sent;
    size_t to_send = (max_bytes < remaining) ? max_bytes : remaining;

    if (to_send > 0 && data->upload_buffer) {
        memcpy(buffer, data->upload_buffer + data->upload_sent, to_send);
        data->upload_sent += to_send;
        data->total_bytes += to_send;
    }

    return to_send;
}

static int transfer_progress_callback(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                                      curl_off_t ultotal, curl_off_t ulnow) {
    struct progress_data *progress = (struct progress_data *)clientp;
    curl_off_t one_mb = 1024 * 1024;
    curl_off_t current_bytes;

    if (!progress) {
        return 0;
    }

    /* Determine if this is upload or download */
    if (ultotal > 0 || ulnow > 0) {
        progress->is_upload = 1;
        current_bytes = ulnow;
    } else {
        progress->is_upload = 0;
        current_bytes = dlnow;
    }

    /* Show progress every 1MB */
    if (current_bytes >= progress->last_bytes_shown + one_mb) {
        if (progress->is_upload) {
            if (ultotal > 0) {
                double percent = (ulnow * 100.0) / ultotal;
                double mb_current = ulnow / (1024.0 * 1024.0);
                double mb_total = ultotal / (1024.0 * 1024.0);
                printf("\rUpload progress: %.2f / %.2f MB (%.1f%%)...",
                       mb_current, mb_total, percent);
            } else {
                double mb_current = ulnow / (1024.0 * 1024.0);
                printf("\rUpload progress: %.2f MB uploaded...", mb_current);
            }
        } else {
            if (dltotal > 0) {
                double percent = (dlnow * 100.0) / dltotal;
                double mb_current = dlnow / (1024.0 * 1024.0);
                double mb_total = dltotal / (1024.0 * 1024.0);
                printf("\rDownload progress: %.2f / %.2f MB (%.1f%%)...",
                       mb_current, mb_total, percent);
            } else {
                double mb_current = dlnow / (1024.0 * 1024.0);
                printf("\rDownload progress: %.2f MB downloaded...", mb_current);
            }
        }
        fflush(stdout);
        progress->last_bytes_shown = current_bytes;
    }

    return 0;
}

void curl_download_test(const char *host) {
    CURL *curl = curl_easy_init();
    if (curl) {
        char url[256];
        strcpy(url, "http://");
        strcat(url, host);
        strcat(url, DOWNLOAD_PATH);

        struct transfer_data data;
        data.total_bytes = 0;
        data.upload_buffer = NULL;
        data.upload_size = 0;
        data.upload_sent = 0;

        struct progress_data progress;
        progress.last_bytes_shown = 0;
        progress.is_upload = 0;

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, download_write_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transfer_progress_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)SPEEDTEST_TIMEOUT_SEC);
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

void curl_upload_test(const char *host) {
    CURL *curl = curl_easy_init();
    if (curl) {
        char url[256];
        strcpy(url, "http://");
        strcat(url, host);
        strcat(url, UPLOAD_PATH);

        /* Generate upload data */
        size_t upload_size = UPLOAD_SIZE_MB * 1024 * 1024;
        char *upload_buffer = malloc(upload_size);
        if (!upload_buffer) {
            fprintf(stderr, "Failed to allocate upload buffer\n");
            curl_easy_cleanup(curl);
            return;
        }

        /* Fill with some data */
        memset(upload_buffer, 'A', upload_size);

        struct transfer_data data;
        data.total_bytes = 0;
        data.upload_buffer = upload_buffer;
        data.upload_size = upload_size;
        data.upload_sent = 0;

        struct progress_data progress;
        progress.last_bytes_shown = 0;
        progress.is_upload = 1;

        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, upload_read_callback);
        curl_easy_setopt(curl, CURLOPT_READDATA, &data);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)upload_size);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transfer_progress_callback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)SPEEDTEST_TIMEOUT_SEC);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");

        printf("Testing upload speed to %s...\n", host);
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
                double mb_uploaded = data.total_bytes / (1024.0 * 1024.0);
                printf("Uploaded %.2f MB in %.2f seconds\n", mb_uploaded, total_time);
                printf("Upload speed: %.2f Mbps\n", speed_mbps);
            } else {
                if (response_code != 200) {
                    printf("Warning: Server returned error code %ld\n", response_code);
                } else {
                    printf("Warning: No data uploaded or time is zero\n");
                }
            }
        } else {
            fprintf(stderr, "Upload failed: %s\n", curl_easy_strerror(res));
        }

        free(upload_buffer);
        curl_easy_cleanup(curl);
    }
}

struct location {
    char *country;
    char *city;
};

/* Detect user's location using geolocation API */
struct location *detect_location(void) {
    CURL *curl = curl_easy_init();
    struct location *loc = NULL;

    if (!curl) {
        fprintf(stderr, "Failed to initialize curl for location detection\n");
        return NULL;
    }

    struct response_data response;
    response.buffer = NULL;
    response.size = 0;

    curl_easy_setopt(curl, CURLOPT_URL, LOCATION_API_URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, api_response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)LOCATION_API_TIMEOUT_SEC);

    CURLcode res = curl_easy_perform(curl);

    if (res == CURLE_OK && response.buffer) {
        cJSON *json = cJSON_Parse(response.buffer);
        if (json) {
            loc = malloc(sizeof(struct location));
            if (loc) {
                loc->country = NULL;
                loc->city = NULL;

                cJSON *country_item = cJSON_GetObjectItem(json, "country");
                if (country_item && cJSON_IsString(country_item)) {
                    const char *country_str = cJSON_GetStringValue(country_item);
                    if (country_str) {
                        loc->country = malloc(strlen(country_str) + 1);
                        if (loc->country) {
                            strcpy(loc->country, country_str);
                        }
                    }
                }

                cJSON *city_item = cJSON_GetObjectItem(json, "city");
                if (city_item && cJSON_IsString(city_item)) {
                    const char *city_str = cJSON_GetStringValue(city_item);
                    if (city_str) {
                        loc->city = malloc(strlen(city_str) + 1);
                        if (loc->city) {
                            strcpy(loc->city, city_str);
                        }
                    }
                }
            }
            cJSON_Delete(json);
        }
    } else {
        fprintf(stderr, "Location detection failed: %s\n", curl_easy_strerror(res));
    }

    free(response.buffer);
    curl_easy_cleanup(curl);

    return loc;
}

int main(int argc, char *argv[]) {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    printf("Detecting location...\n");
    struct location *loc = detect_location();
    if (loc) {
        printf("Country: %s\n", loc->country ? loc->country : "Unknown");
        printf("City: %s\n", loc->city ? loc->city : "Unknown");
    } else {
        printf("Failed to detect location\n");
    }


    /* Find best server */
    cJSON *json = read_json_file("speedtest_server_list.json");
    if (json && cJSON_IsArray(json)) {
        int count = cJSON_GetArraySize(json);
        printf("Found %d servers\n", count);
        printf("Finding best server...\n");

        const char *user_country = loc ? loc->country : NULL;
        const char *user_city = loc ? loc->city : NULL;
        cJSON *best_server = find_best_server(json, user_country, user_city);
        if (best_server) {
            cJSON *host_item = cJSON_GetObjectItem(best_server, "host");
            cJSON *country_item = cJSON_GetObjectItem(best_server, "country");
            cJSON *city_item = cJSON_GetObjectItem(best_server, "city");
            const char *host = cJSON_GetStringValue(host_item);
            const char *country = cJSON_GetStringValue(country_item);
            const char *city = cJSON_GetStringValue(city_item);

            printf("Best server: %s", host ? host : "Unknown");
            if (country && city) {
                printf(" (%s, %s)", country, city);
            }
            printf("\n");
        } else {
            printf("No suitable server found\n");
        }

        cJSON_Delete(json);
    }

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

    curl_global_cleanup();
    if (loc) {
        free(loc->country);
        free(loc->city);
        free(loc);
    }

    return EXIT_SUCCESS;
}
