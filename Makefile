all: main

main: src/main.cpp
	g++ -std=c++14 -Wall -g -I ./include -o sleep_service ./src/main.cpp ./src/roles/* ./src/message/*  -lpthread -lncurses

clean: 
	rm -f sleep_service