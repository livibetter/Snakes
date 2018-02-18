TARGET = snake
LIBS = -lm -lrt 
LIBS += $(if $(shell pkg-config --exists ncursesw && echo y),\
	$(shell pkg-config --libs ncursesw),\
	$(if $(shell pkg-config --exists ncurses && echo y),\
		$(shell pkg-config --libs ncurses),-lcurses))
CFLAGS = -Wall -std=c99

SRCS = snake.c pipe.c util.c render.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o$@ $(CFLAGS) $^ $(LIBS)

.c.o:
	$(CC) $(CFLAGS) -o$@ -c $<

clean:
	-rm -f $(TARGET) $(OBJS)

.PHONY: all clean