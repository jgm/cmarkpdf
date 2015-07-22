CCFLAGS = -g
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
  CCFLAGS += -D MAIN_FONT_PATH='"/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"' \
	     -D TT_FONT_PATH='"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"'
endif
ifeq ($(UNAME_S),Darwin)
  CCFLAGS += -D MAIN_FONT_PATH='"/Library/Fonts/Georgia.ttf"' \
	     -D TT_FONT_PATH='"/Library/Fonts/Andale Mono.ttf"'
endif

.PHONY: all clean leakcheck

all: cmarkpdf

%.o: src/%.c
	$(CC) -Wall -c $< -o $@ $(CCFLAGS)

cmarkpdf: cmarkpdf.o pdf.o
	$(CC) $^ -o $@ $(CCFLAGS) -lhpdf -lcmark

leakcheck:
	valgrind -q --leak-check=full --dsymutil=yes --error-exitcode=1 ./cmarkpdf -o leakcheck.pdf alltests.md

clean:
	-rm *.o cmarkpdf
