CC       := gcc
CFLAGS   := -Wall -O2
LDFLAGS  := -lssl -lcrypto -lpthread

TARGETS  := sendfile_user_server sendfile_user_client

all: $(TARGETS)

sendfile_user_server: sendfile_user_server.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

sendfile_user_client: sendfile_user_client.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o








#CC      ?= $(CROSS_COMPILE)gcc
#CFLAGS  := -Wall -Wextra -O2 -std=c11 -pthread
#LDLIBS  := -lcrypto -pthread

#CLIENT  := sendfile_user_client
#SERVER  := sendfile_user_server

#all: $(CLIENT) $(SERVER)

#$(CLIENT): sendfile_user_client.o
#	$(CC) $^ -o $@ $(LDLIBS)

#$(SERVER): sendfile_user_server.o
#	$(CC) $^ -o $@ $(LDLIBS)

#%.o: %.c
#	$(CC) $(CFLAGS) -c $< -o $@

#clean:
#	rm -f *.o $(CLIENT) $(SERVER)

