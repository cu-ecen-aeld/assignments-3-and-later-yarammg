SRC = aesdsocket.c
CC= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Werror -g
LDFLAGS ?= -lpthread
all: $(TARGET)
	$(CC) $(SRC) -o aesdsocket $(CFLAGS) $(LDFLAGS)


clean:
	rm -f aesdsocket.o aesdsocket
