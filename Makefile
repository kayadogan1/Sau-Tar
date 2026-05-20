CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
TARGET = tarsau
SRC = tarsau.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

test: $(TARGET)
	rm -rf testler/cikti testler/ornek.sau
	./$(TARGET) -b testler/t1 testler/t2 testler/t3 testler/t4.txt testler/t5.dat -o testler/ornek.sau
	./$(TARGET) -a testler/ornek.sau testler/cikti
	diff -q testler/t1 testler/cikti/t1
	diff -q testler/t2 testler/cikti/t2
	diff -q testler/t3 testler/cikti/t3
	diff -q testler/t4.txt testler/cikti/t4.txt
	diff -q testler/t5.dat testler/cikti/t5.dat

clean:
	rm -f $(TARGET)
	rm -rf testler/cikti testler/ornek.sau
