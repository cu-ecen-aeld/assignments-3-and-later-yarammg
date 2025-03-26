SRC = aesdsocket.c
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Werror -g -DUSE_AESD_CHAR_DEVICE
LDFLAGS ?= -lpthread
all: $(TARGET)
	$(CC) $(SRC) -o aesdsocket $(CFLAGS) $(LDFLAGS)


clean:
	rm -f aesdsocket.o aesdsocket
