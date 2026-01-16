#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
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
        fprintf(stderr, "Parse error: %s\n", cJSON_GetErrorPtr());
        free(buffer);
        exit(EXIT_FAILURE);
    }
    free(buffer);

    return json;
}

int main(int argc, char *argv[]) {
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

    return EXIT_SUCCESS;
}
