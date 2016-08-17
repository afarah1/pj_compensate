CC=gcc
STD=-std=c99
WARN=-Wall -Wextra -Wpedantic -Wformat-security -Wshadow -Wconversion \
		 -Wfloat-equal #-Wpadded -Winline
OPT=-O2 -march=native -ffinite-math-only -fno-signed-zeros -DLOG_LEVEL=LOG_LEVEL_WARNING
DBG=-O0 -g -ggdb -DLOG_LEVEL=LOG_LEVEL_DEBUG
LIB=$(shell pkg-config gsl --libs)
INC=-I./include
EXTRA=-DVERSION=\"$(shell git describe --abbrev=4 --dirty --always --tags)\"
FLAGS=$(STD) $(WARN) $(OPT) $(EXTRA) $(INC) $(LIB)

all: pj_compensate

pj_compensate:
	$(CC) -c src/hist.c $(FLAGS) -Wno-float-equal
	$(CC) -c src/events.c $(FLAGS) -Wno-float-equal
	$(CC) -c src/reader.c $(FLAGS)
	$(CC) -c src/queue.c $(FLAGS)
	$(CC) -c src/compensation.c $(FLAGS) -Wno-float-equal
	$(CC) src/pj_compensate.c hist.o events.o reader.o queue.o compensation.o \
		-o pj_compensate $(FLAGS)
	rm -f hist.o events.o reader.o queue.o compensation.o

clean:
	rm -f hist.o events.o reader.o queue.o compensation.o pj_compensate
