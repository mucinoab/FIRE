fire: fire.c appendBuffer.c
	$(CC) fire.c -o fire -O2 -march=native -fwhole-program -flto -Wall -Wextra -pedantic -std=c17 -lm
