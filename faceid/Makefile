CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra

all: howdy-compare-wrapper howdy-cli-wrapper

howdy-compare-wrapper: howdy_compare_wrapper.c
	$(CC) $(CFLAGS) -o $@ $<

howdy-cli-wrapper: howdy_cli_wrapper.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f howdy-compare-wrapper howdy-cli-wrapper
