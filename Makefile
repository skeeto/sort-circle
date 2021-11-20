.POSIX:
CC     = cc -std=c99
CFLAGS = -Wall -Wextra -Ofast -march=native
LDLIBS = -lm

sort$(EXE): sort.c font.h
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ sort.c $(LDLIBS)

clean:
	rm -f sort$(EXE)
