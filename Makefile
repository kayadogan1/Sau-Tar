CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
TARGET = tarsau
SRC = tarsau.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
