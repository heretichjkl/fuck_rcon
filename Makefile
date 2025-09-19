SRC=main.c
CFLAGS=-O2
LFLAGS=
PROGRAM=rcon-cli

all:
	${CC} ${SRC} -o ${PROGRAM} ${CFLAGS} ${LFLAGS}
