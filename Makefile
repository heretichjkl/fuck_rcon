SRC=main.c
CFLAGS=-O2
LFLAGS=
PROGRAM=rcon-cli

ifeq ($(OS), Windows_NT)
	LFLAGS += -lws2_32
endif

all:
	${CC} ${SRC} -o ${PROGRAM} ${CFLAGS} ${LFLAGS}
