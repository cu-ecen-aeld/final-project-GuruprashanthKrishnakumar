SOURCE = ./main.c
OUTPUT = ./heartrate_monitoring_server

CROSS_COMPILE =

CC?=$(CROSS_COMPILE)gcc
CFLAGS?= -g -Wall -Werror
LDFLAGS?= -pthread -lrt

all: hm11

hm11: $(SOURCE)
	$(CC) $(CFLAGS) $^ -o ./heartrate_monitoring_server $(INCLUDES) $(LDFLAGS)

clean:
	rm -f $(OUTPUT)
	rm -f *.o