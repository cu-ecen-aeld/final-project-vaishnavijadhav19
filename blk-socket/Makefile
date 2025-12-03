

CC      ?= $(CROSS_COMPILE)gcc
CFLAGS  := -Wall -Wextra -O2 -std=c11 -pthread
LDLIBS  := -lcrypto -pthread

CLIENT  := sendfile_user_client
SERVER  := sendfile_user_server

all: $(CLIENT) $(SERVER)

$(CLIENT): sendfile_user_client.o
	$(CC) $^ -o $@ $(LDLIBS)

$(SERVER): sendfile_user_server.o
	$(CC) $^ -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o $(CLIENT) $(SERVER)

