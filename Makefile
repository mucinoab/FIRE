fire: fire.c base.c appendBuffer.c normalMode.c insertMode.c Makefile
	$(CC) fire.c -o fire -O2 -march=native -ffast-math -fwhole-program -flto -Wall -Wextra -pedantic -std=c17 -lm
