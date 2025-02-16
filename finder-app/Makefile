writer : writer.o

writer.o: writer.c
	gcc -c writer.c

CROSS_COMPILE:
	aarch64-none-linux-gnu-gcc -g -Wall -c -o writer.o writer.c

clean:
	rm -f writer.o writer
