SHELL = /bin/sh

CFLAGS = -g -Dlinux

LIBPATH = 

LIBS = -lpthread

CPPFLAGS = 

CC = gcc -g -D_REENTRANT $(CPPFLAGS)


DOBJS=\
errexit.o \
passivesock.o \
passiveUDP.o \
tthread.o \
UDPechod.o

EOBJS=\
errexit.o \
connectsock.o \
connectUDP.o \
tthread.o \
UDPecho.o

E2OBJS=\
errexit.o \
passivesock.o \
passiveUDP.o \
tthread.o \
UDPecho2.o

all: UDPechod UDPecho UDPecho2

UDPechod:	$(DOBJS)
	${CC} -o $@ $(DOBJS) ${LIBS}

UDPecho:	$(EOBJS)
	${CC} -o $@ $(EOBJS) ${LIBS}

UDPecho2:	$(E2OBJS)
	${CC} -o $@ $(E2OBJS) ${LIBS}

clean:
	rm *.o *~ UDPechod UDPecho UDPecho2
