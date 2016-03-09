CC=gcc
STD=-std=c99
WARN=-Wall -Wextra -Wpedantic -Wformat-security -Wshadow -Wconversion \
		 -Wfloat-equal #-Wpadded -Winline
OPT=-O2 -march=native -ffinite-math-only -fno-signed-zeros
DBG=-O0 -g -ggdb
INC=-I./include
EXTRA=
FLAGS=$(STD) $(WARN) $(OPT) $(EXTRA) $(INC) -lgsl -lgslcblas -lm

all:
	$(CC) -c src/hist.c $(FLAGS) -Wno-float-equal
	$(CC) -c src/events.c $(FLAGS) -Wno-float-equal
	$(CC) -c src/reader.c $(FLAGS)
	$(CC) -c src/queue.c $(FLAGS)
	$(CC) -c src/compensation.c $(FLAGS)
	$(CC) src/pj_compensate.c hist.o events.o reader.o queue.o compensation.o \
		-o pj_compensate $(FLAGS)
	rm -f hist.o events.o reader.o queue.o compensation.o

clean:
	rm -f hist.o events.o reader.o queue.o compensation.o pj_compensate
