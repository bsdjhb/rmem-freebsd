PROG= test_mtx_spin
SRCS= kern_mutex.c test_mtx_spin.c

CFLAGS=-target aarch64-unknown-freebsd12.0
CFLAGS+=-B/usr/local/aarch64-freebsd/bin
CFLAGS+=-g -O2 -Wall -save-temps=obj -ffreestanding -nostdlib -Wl,--defsym,_start=main -Wa,-L
CFLAGS+=-nostdinc -I ../include -DSMP -D_KERNEL

all: ${PROG}

test_mtx_spin: ${SRCS}
	${CC} ${CFLAGS} ${SRCS} -o ${PROG}


CLEANFILES= ${SRCS:.c=.o} ${SRCS:.c=.bc} ${SRCS:.c=.i} ${SRCS:.c=.s} ${PROG}

clean:
	rm ${CLEANFILES}
