CCFLAGS = -g
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
  CCFLAGS += -D _LINUX
endif
ifeq ($(UNAME_S),Darwin)
  CCFLAGS += -D _OSX
endif

.PHONY: all clean leakcheck

all: cmarkpdf

%.o: src/%.c
	$(CC) -Wall -c $< -o $@ $(CCFLAGS)

cmarkpdf: main.o pdf.o
	$(CC) $^ -o $@ $(CCFLAGS) -lhpdf -lcmark

leakcheck:
	valgrind -q --leak-check=full --dsymutil=yes --error-exitcode=1 ./cmarkpdf -o leakcheck.pdf alltests.md

clean:
	-rm *.o cmarkpdf
