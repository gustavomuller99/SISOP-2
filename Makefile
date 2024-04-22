all: main

main: src/main.cpp
	g++ -std=c++14 -Wall -g -I ./include -o main ./src/main.cpp ./src/roles/* ./src/message/*  -lpthread

clean: 
	rm -f main