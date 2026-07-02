CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -pthread
PYTHON  = python3
TARGETS = stress_client

.PHONY: all integration clean

all: $(TARGETS)

integration:
	$(PYTHON) integration_suite.py

stress_client: stress_client.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)
