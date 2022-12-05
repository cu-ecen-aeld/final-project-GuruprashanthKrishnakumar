SOURCE = ./main.c
OUTPUT = ./hm11_extrafeatures

CROSS_COMPILE =

CC?=$(CROSS_COMPILE)gcc
CFLAGS?= -g -Wall -Werror
LDFLAGS?= -pthread -lrt

all: hm11

hm11: $(SOURCE)
	$(CC) $(CFLAGS) $^ -o ./hm11_extrafeatures $(INCLUDES) $(LDFLAGS)

clean:
	rm -f $(OUTPUT)
	rm -f *.o