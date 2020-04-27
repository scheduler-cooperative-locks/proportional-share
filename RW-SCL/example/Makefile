#Use machine's CPU-SPEED, else results will be wrong.
#example CYCLE_PER_US=2400L - 2.4 GHz processor

#CYCLE_PER_US=2400L
ifndef CYCLE_PER_US
$(error CYCLE_PER_US not set. Set CYCLE_PER_US according to your machine CPU-SPEED)
endif

OFLAG=-O3
FLAGS=-I../ -g -lpthread -Wall ${OFLAG} -DCYCLE_PER_US=${CYCLE_PER_US} -g

read_pref:
	gcc main.c -o main_read ${FLAGS} -DREAD_PREF -DPTHREAD_RW

write_pref:
	gcc main.c -o main_write ${FLAGS} -DWRITE_PREF -DPTHREAD_RW

rwlock_scl:
	gcc main.c -o main_scl ${FLAGS} -DRWLOCK_SCL

clean:
	rm main_*
