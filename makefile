CC=g++ -std=c++11

default:
	$(CC) -Wall -Wextra -g -o server -I. server.cpp
	$(CC) -Wall -Wextra -g -o client -I. client.cpp
