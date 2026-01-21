# cJSON uses C89 standard
CFLAGS=-std=c89
CFLAGS += -Wall
CFLAGS += -Wextra
CFLAGS += -Werror

LDFLAGS=-lcurl

main: src/main.c
	$(CC) $(CFLAGS) src/main.c src/cJSON.c -o main $(LDFLAGS)

.PHONY: clean
clean:
	rm -f main
