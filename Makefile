CC=gcc -Wall -g3 -w -o $@
COMPILE=-c $<
VPATH=header
BIN=encrypt

#Build all
all: ${BIN}

${BIN}: ${BIN}.o
	${CC} $^ -lpthread

%.o: %.c
	${CC} -I ${VPATH} ${COMPILE}

clean:
	rm -rf *.so *.o ${BIN}

