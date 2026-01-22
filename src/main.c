#include "cJSON.h"
#include <curl/curl.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Constants */
#define SPEEDTEST_TIMEOUT_SEC 15
#define UPLOAD_SIZE_MB 30
#define LOCATION_API_URL "http://ip-api.com/json/"
#define LOCATION_API_TIMEOUT_SEC 10
#define DOWNLOAD_PATH "/speedtest/random4000x4000.jpg"
#define UPLOAD_PATH "/speedtest/upload.php"
#define MAX_URL_LENGTH 256

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

struct location {
    char *country;
    char *city;
};

static size_t download_write_callback(char *buffer, size_t size, size_t nitems,
                                      void *outstream) {
    (void)buffer;
    struct transfer_data *data = (struct transfer_data *)outstream;
    size_t realsize = size * nitems;
    data->total_bytes += realsize;
    return realsize;
}

static size_t discard_response_callback(char *buffer, size_t size, size_t nitems,
                                        void *outstream) {
    (void)buffer;
    (void)outstream;
    /* Discard server response without modifying any data */
    return size * nitems;
}

static size_t api_response_callback(char *buffer, size_t size, size_t nitems,
                                    void *outstream) {
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

static size_t upload_read_callback(char *buffer, size_t size, size_t nitems,
                                   void *instream) {
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

static int transfer_progress_callback(void *clientp, curl_off_t dltotal,
                                      curl_off_t dlnow, curl_off_t ultotal,
                                      curl_off_t ulnow) {
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
                printf("\rUpload progress: %.2f / %.2f MB (%.1f%%)...", mb_current,
                       mb_total, percent);
            } else {
                double mb_current = ulnow / (1024.0 * 1024.0);
                printf("\rUpload progress: %.2f MB uploaded...", mb_current);
            }
        } else {
            if (dltotal > 0) {
                double percent = (dlnow * 100.0) / dltotal;
                double mb_current = dlnow / (1024.0 * 1024.0);
                double mb_total = dltotal / (1024.0 * 1024.0);
                printf("\rDownload progress: %.2f / %.2f MB (%.1f%%)...", mb_current,
                       mb_total, percent);
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

cJSON *read_json_file(const char *filename) {
    FILE *stream = fopen(filename, "r");
    if (!stream) {
        fprintf(stderr, "Error opening file: %s\n", filename);
        return NULL;
    }

    /* Get file size */
    fseek(stream, 0, SEEK_END);
    long size = ftell(stream);
    fseek(stream, 0, SEEK_SET);

    if (size <= 0) {
        fprintf(stderr, "Error: Invalid file size: %s\n", filename);
        fclose(stream);
        return NULL;
    }

    /* Read into buffer */
    char *buffer = malloc(size + 1);
    if (!buffer) {
        fprintf(stderr, "Error: Failed to allocate memory for file: %s\n", filename);
        fclose(stream);
        return NULL;
    }

    size_t bytes_read = fread(buffer, 1, size, stream);
    if (bytes_read != (size_t)size) {
        fprintf(stderr, "Error: Failed to read file completely: %s\n", filename);
        free(buffer);
        fclose(stream);
        return NULL;
    }

    buffer[size] = '\0';
    fclose(stream);

    cJSON *json = cJSON_Parse(buffer);
    if (!json) {
        fprintf(stderr, "Parse error: %.100s\n", cJSON_GetErrorPtr());
        free(buffer);
        return NULL;
    }
    free(buffer);

    return json;
}

static int test_server_reachable(const char *host) {
    CURL *curl = curl_easy_init();
    int reachable = 0;

    if (!curl) {
        return 0;
    }

    char url[MAX_URL_LENGTH];
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
static cJSON *find_best_server(cJSON *json_array, const char *user_country,
                               const char *user_city) {
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

/* Test download speed and return speed in Mbps, or -1.0 on failure */
double test_download_speed(const char *host) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return -1.0;
    }

    char url[MAX_URL_LENGTH];
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

    double speed_mbps = -1.0;
    double total_time;
    long response_code;

    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_time);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    /* Handle timeout: calculate speed from data transferred before timeout */
    if (res == CURLE_OPERATION_TIMEDOUT) {
        if (data.total_bytes > 0 && total_time > 0) {
            double speed_bps = (data.total_bytes * 8.0) / total_time;
            speed_mbps = speed_bps / 1000000.0;
            double mb_downloaded = data.total_bytes / (1024.0 * 1024.0);
            printf("Downloaded %.2f MB in %.2f seconds (timeout reached)\n",
                   mb_downloaded, total_time);
        } else {
            printf("Warning: Timeout reached but no data was downloaded\n");
        }
    } else if (res == CURLE_OK) {
        if (response_code == 200 && total_time > 0 && data.total_bytes > 0) {
            double speed_bps = (data.total_bytes * 8.0) / total_time;
            speed_mbps = speed_bps / 1000000.0;
            double mb_downloaded = data.total_bytes / (1024.0 * 1024.0);
            printf("Downloaded %.2f MB in %.2f seconds\n", mb_downloaded, total_time);
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
    return speed_mbps;
}

/* Test upload speed and return speed in Mbps, or -1.0 on failure */
double test_upload_speed(const char *host) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        return -1.0;
    }

    char url[MAX_URL_LENGTH];
    strcpy(url, "http://");
    strcat(url, host);
    strcat(url, UPLOAD_PATH);

    /* Generate upload data */
    size_t upload_size = UPLOAD_SIZE_MB * 1024 * 1024;
    char *upload_buffer = malloc(upload_size);
    if (!upload_buffer) {
        fprintf(stderr, "Failed to allocate upload buffer\n");
        curl_easy_cleanup(curl);
        return -1.0;
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
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, discard_response_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, transfer_progress_callback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)SPEEDTEST_TIMEOUT_SEC);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");

    printf("Testing upload speed to %s...\n", host);
    CURLcode res = curl_easy_perform(curl);
    printf("\n");

    double speed_mbps = -1.0;
    double total_time;
    long response_code;

    curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_time);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);

    /* Handle timeout: calculate speed from data transferred before timeout */
    if (res == CURLE_OPERATION_TIMEDOUT) {
        if (data.total_bytes > 0 && total_time > 0) {
            double speed_bps = (data.total_bytes * 8.0) / total_time;
            speed_mbps = speed_bps / 1000000.0;
            double mb_uploaded = data.total_bytes / (1024.0 * 1024.0);
            printf("Uploaded %.2f MB in %.2f seconds (timeout reached)\n",
                   mb_uploaded, total_time);
        } else {
            printf("Warning: Timeout reached but no data was uploaded\n");
        }
    } else if (res == CURLE_OK) {
        if (response_code == 200 && total_time > 0 && data.total_bytes > 0) {
            double speed_bps = (data.total_bytes * 8.0) / total_time;
            speed_mbps = speed_bps / 1000000.0;
            double mb_uploaded = data.total_bytes / (1024.0 * 1024.0);
            printf("Uploaded %.2f MB in %.2f seconds\n", mb_uploaded, total_time);
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
    return speed_mbps;
}

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

static void print_usage(const char *program_name) {
    printf("Usage: %s [OPTIONS]\n\n", program_name);
    printf("Options:\n");
    printf("  -d, --download <server>  Test download speed with specified server\n");
    printf("  -u, --upload <server>    Test upload speed with specified server\n");
    printf("  -s, --server             Find best server by location\n");
    printf("  -l, --location           Detect user location\n");
    printf("  -a, --automated          Run full automated test\n");
    printf("  -h, --help               Show this help message\n");
}

int main(int argc, char *argv[]) {
    curl_global_init(CURL_GLOBAL_DEFAULT);

    int option;
    int option_index = 0;
    int do_download = 0;
    int do_upload = 0;
    int do_find_server = 0;
    int do_location = 0;
    int do_automated = 0;
    const char *download_server = NULL;
    const char *upload_server = NULL;

    static struct option long_options[] = {
        {"download", required_argument, 0, 'd'},
        {"upload", required_argument, 0, 'u'},
        {"server", no_argument, 0, 's'},
        {"location", no_argument, 0, 'l'},
        {"automated", no_argument, 0, 'a'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};

    while ((option = getopt_long(argc, argv, "d:u:slah", long_options,
                                 &option_index)) != -1) {
        switch (option) {
            case 'd':
                do_download = 1;
                download_server = optarg;
                if (!download_server || strlen(download_server) == 0) {
                    fprintf(stderr, "Error: --download requires a server host\n");
                    print_usage(argv[0]);
                    curl_global_cleanup();
                    return EXIT_FAILURE;
                }
                break;
            case 'u':
                do_upload = 1;
                upload_server = optarg;
                if (!upload_server || strlen(upload_server) == 0) {
                    fprintf(stderr, "Error: --upload requires a server host\n");
                    print_usage(argv[0]);
                    curl_global_cleanup();
                    return EXIT_FAILURE;
                }
                break;
            case 's':
                do_find_server = 1;
                break;
            case 'l':
                do_location = 1;
                break;
            case 'a':
                do_automated = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                curl_global_cleanup();
                return EXIT_SUCCESS;
            default:
                print_usage(argv[0]);
                curl_global_cleanup();
                return EXIT_FAILURE;
        }
    }

    /* If no options provided, show usage */
    if (!do_download && !do_upload && !do_find_server && !do_location &&
        !do_automated) {
        print_usage(argv[0]);
        curl_global_cleanup();
        return EXIT_FAILURE;
    }

    struct location *loc = NULL;
    cJSON *json = NULL;
    cJSON *best_server = NULL;
    const char *test_server_host = NULL;
    double download_speed = -1.0;
    double upload_speed = -1.0;

    if (do_automated) {
        /* 1. Detect location */
        printf("Detecting location...\n");
        loc = detect_location();
        if (loc) {
            printf("Location detected: %s", loc->country ? loc->country : "Unknown");
            if (loc->city) {
                printf(", %s", loc->city);
            }
            printf("\n");
        } else {
            printf("Warning: Failed to detect location, continuing anyway...\n");
        }
        printf("\n");

        /* 2. Find best server */
        printf("Finding best server...\n");
        json = read_json_file("speedtest_server_list.json");
        if (!json || !cJSON_IsArray(json)) {
            printf("Error: Failed to read or parse server list\n");
        } else {
            int count = cJSON_GetArraySize(json);
            printf("Found %d servers in list\n", count);

            const char *user_country = loc ? loc->country : NULL;
            const char *user_city = loc ? loc->city : NULL;
            best_server = find_best_server(json, user_country, user_city);
            if (!best_server) {
                printf("Error: No suitable server found\n");
            } else {
                cJSON *host_item = cJSON_GetObjectItem(best_server, "host");
                test_server_host = cJSON_GetStringValue(host_item);
                if (!test_server_host) {
                    printf("Error: Best server has no host\n");
                } else {
                    printf("Best server selected: %s\n", test_server_host);
                    printf("\n");

                    /* 3. Download test */
                    download_speed = test_download_speed(test_server_host);
                    printf("\n");

                    /* 4. Upload test */
                    upload_speed = test_upload_speed(test_server_host);
                    printf("\n");

                    /* 5. Print final results */
                    printf("Results:\n");
                    printf("========\n");
                    if (download_speed >= 0.0) {
                        printf("Download speed: %.2f Mbps\n", download_speed);
                    } else {
                        printf("Download speed: Failed\n");
                    }
                    if (upload_speed >= 0.0) {
                        printf("Upload speed: %.2f Mbps\n", upload_speed);
                    } else {
                        printf("Upload speed: Failed\n");
                    }
                    if (test_server_host) {
                        printf("Server: %s\n", test_server_host);
                    }
                    if (loc && loc->country) {
                        printf("Location: %s\n", loc->country);
                    }
                    printf("\n");
                }
            }
        }
    } else {
        if (do_location) {
            printf("Detecting location...\n");
            loc = detect_location();
            if (loc) {
                printf("Country: %s\n", loc->country ? loc->country : "Unknown");
                if (loc->city) {
                    printf("City: %s\n", loc->city);
                }
            } else {
                printf("Failed to detect location\n");
            }
        }

        if (do_find_server) {
            printf("Finding best server...\n");
            if (!loc) {
                loc = detect_location();
            }
            json = read_json_file("speedtest_server_list.json");
            if (json && cJSON_IsArray(json)) {
                int count = cJSON_GetArraySize(json);
                printf("Found %d servers in list\n", count);

                const char *user_country = loc ? loc->country : NULL;
                const char *user_city = loc ? loc->city : NULL;
                best_server = find_best_server(json, user_country, user_city);
                if (best_server) {
                    cJSON *host_item = cJSON_GetObjectItem(best_server, "host");
                    cJSON *country_item = cJSON_GetObjectItem(best_server, "country");
                    cJSON *city_item = cJSON_GetObjectItem(best_server, "city");
                    const char *host = cJSON_GetStringValue(host_item);
                    const char *country = cJSON_GetStringValue(country_item);
                    const char *city = cJSON_GetStringValue(city_item);

                    printf("Best server: %s", host ? host : "Unknown");
                    if (country) {
                        printf(" (%s, %s)", country, city);
                    }
                    printf("\n");
                } else {
                    printf("No suitable server found\n");
                }
            } else {
                fprintf(stderr, "Error: Failed to read or parse server list\n");
            }
        }
        if (do_download) {
            double speed = test_download_speed(download_server);
            if (speed >= 0.0) {
                printf("Download speed: %.2f Mbps\n", speed);
            }
        }
        if (do_upload) {
            double speed = test_upload_speed(upload_server);
            if (speed >= 0.0) {
                printf("Upload speed: %.2f Mbps\n", speed);
            }
        }
    }

    /* Cleanup */
    if (json) {
        cJSON_Delete(json);
    }
    curl_global_cleanup();
    if (loc) {
        if (loc->country) {
            free(loc->country);
        }
        if (loc->city) {
            free(loc->city);
        }
        free(loc);
    }

    return EXIT_SUCCESS;
}
