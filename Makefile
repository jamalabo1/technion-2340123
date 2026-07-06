#
# To compile, type "make" or make "all"
# To remove files, type "make clean"
#

SERVER_OBJS = server.o request.o segel.o log.o requests_queue.o formatter.o
CLIENT_OBJS = client.o segel.o requests_queue.o formatter.o
OBJS = $(SERVER_OBJS) $(CLIENT_OBJS)
TARGET = server

CC = gcc
CFLAGS = -g -Wall

LIBS = -lpthread

.SUFFIXES: .c .o

all: server client output.cgi
	-mkdir -p public
	-cp output.cgi favicon.ico home.html public

server: $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o server $(SERVER_OBJS) $(LIBS)

client: $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o client $(CLIENT_OBJS)

output.cgi: output.c
	$(CC) $(CFLAGS) -o output.cgi output.c

.c.o:
	$(CC) $(CFLAGS) -o $@ -c $<

clean:
	-rm -f $(OBJS) server client output.cgi
	-rm -rf public
