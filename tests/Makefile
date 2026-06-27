CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -pthread
TARGETS = test_client stress_client

.PHONY: all clean

all: $(TARGETS)

test_client: test_client.c
	$(CC) $(CFLAGS) -o $@ $<

stress_client: stress_client.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)
