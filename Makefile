DEMOS=text_demo text_demo2

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
  CCFLAGS += -D MAIN_FONT_PATH='"/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf"' \
	     -D TT_FONT_PATH='"/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"'
endif
ifeq ($(UNAME_S),Darwin)
  CCFLAGS += -D MAIN_FONT_PATH='"/Library/Fonts/Georgia.ttf"' \
	     -D TT_FONT_PATH='"/Library/Fonts/Andale Mono.ttf"'
endif

.PHONY: all clean

all: cmarkpdf $(DEMOS)

%.o: %.c
	$(CC) -Wall -c $< -o $@ $(CCFLAGS)

text_demo: text_demo.o
	$(CC) -Wall $^ -o $@ -lhpdf -lm

text_demo2: text_demo2.o
	$(CC) -Wall $^ -o $@ -lhpdf -lm

cmarkpdf: cmarkpdf.o pdf.o
	$(CC) $^ -o $@ $(CCFLAGS) -lhpdf -lcmark

clean:
	-rm *.pdf *.o cmarkpdf $(DEMOS)
