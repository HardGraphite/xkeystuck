CC ?= gcc

TARGET ?= xkeystuck
SRC = $(wildcard *.c)
BIN = ${TARGET}

${BIN}: ${SRC}
	${CC} -Wall -Wextra -O2 -pthread -lX11 -o $@ $^

.PHONY: clean
clean:
	rm ${BIN}
