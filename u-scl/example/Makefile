#Use machine's CPU-SPEED, else results will be wrong.
#example CYCLE_PER_US=2400L - 2.4 GHz processor

#CYCLE_PER_US=2400L
ifndef CYCLE_PER_US
$(error CYCLE_PER_US not set. Set CYCLE_PER_US according to your machine CPU-SPEED)
endif

CC = gcc
GCCVERSIONGTEQ5 := $(shell expr `gcc -dumpversion | cut -f1 -d.` \>= 5)

# u-SCL is not tested with -O3 flag for gcc version above 5 yet.
ifeq "$(GCCVERSIONGTEQ5)" "1"
    OFLAG=-O2
else
    OFLAG=-O3
endif

FLAGS=-I../ -g -lpthread -Wall ${OFLAG} -DCYCLE_PER_US=${CYCLE_PER_US}

fairlock:
	gcc main.c -o main ${FLAGS} -DFAIRLOCK

mutex:
	gcc main.c -o main ${FLAGS} -DMUTEX

spin:
	gcc main.c -o main ${FLAGS} -DSPIN

clean:
	rm main
