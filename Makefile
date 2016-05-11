all: main

main: main.c
	gcc -std=gnu99 -o $@ $^ -lelf -lbsd

clean:
	rm -rf main
