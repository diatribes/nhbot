.PHONY: all clean

all: nhbot

nhbot: main.c tmt.c
	gcc -g -Werror -Wall -Wextra -pedantic -Wno-unused-variable \
		main.c tmt.c -lm -o nhbot

clean:
	rm nhbot 
