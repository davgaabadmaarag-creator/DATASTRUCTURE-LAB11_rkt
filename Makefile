CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
PYTHON ?= python3

EXE :=
ifeq ($(OS),Windows_NT)
EXE := .exe
LDLIBS += -lshell32
endif

.PHONY: all test clean

all: mygrep$(EXE)

mygrep$(EXE): mygrep.c search.c search.h
	$(CC) $(CPPFLAGS) $(CFLAGS) mygrep.c search.c $(LDFLAGS) $(LDLIBS) -o $@

test:
	$(PYTHON) tests/grade.py

clean:
	rm -f mygrep mygrep.exe
	rm -rf .grading
