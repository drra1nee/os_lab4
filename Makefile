CC      = gcc
CFLAGS  = -std=c11 -O2 -Wall -Wextra -Wpedantic -Iinclude
LDFLAGS =

TARGETS = parent.exe child1.exe child2.exe

PARENT_OBJS = build/parent.o build/channel.o build/utils.o
CHILD_OBJS  = build/child.o build/channel.o build/utils.o build/vowels.o

.PHONY: all clean

all: $(TARGETS)

parent.exe: $(PARENT_OBJS)
	$(CC) $(PARENT_OBJS) -o $@ $(LDFLAGS)

# child1 и child2 собираются из одного исходника, как в лабораторной работе 2
child1.exe child2.exe: $(CHILD_OBJS)
	$(CC) $(CHILD_OBJS) -o $@ $(LDFLAGS)

build/%.o: src/%.c include/*.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir build

# работает и в cmd (mingw32-make), и в sh/bash
clean:
	-rm -rf build $(TARGETS) 2>/dev/null || (del /f /q $(TARGETS) & rmdir /s /q build)
