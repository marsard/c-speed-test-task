#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "cJSON.h"

cJSON* read_json_file(const char* filename) {
    return NULL;
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
