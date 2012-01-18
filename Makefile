CC = g++
FLAGS = -Wall -pthread -O3

all:
	$(CC) $(FLAGS) irrealvm.cpp -o irrealvm
	
