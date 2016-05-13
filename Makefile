CC = cc
CFLAGS = -c -Iinclude -std=c99 -pthread -D_POSIX_SOURCE -Wall
LD = $(CC)
LDFLAGS = -lgc -lm -L. -lcheax

TARGET = libcheax.a
REPL = cheaky

OBJECTS = src/read.o src/eval.o src/builtins.o
REPL_OBJECTS = cheaky.o

release: CFLAGS += -DNDEBUG -Ofast
debug: CFLAGS += -g

release debug: $(TARGET)

$(TARGET): $(OBJECTS)
	ar rcs $(TARGET) $(OBJECTS)

$(REPL): $(REPL_OBJECTS)
	$(LD) $(REPL_OBJECTS) -o $(REPL) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f $(TARGET) $(OBJECTS) $(REPL) $(REPL_OBJECTS)

