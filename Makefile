DEMOS=text_demo

.PHONY: all clean

all: cmarkpdf $(DEMOS)

%.o: %.c
	$(CC) -Wall -c $< -o $@

text_demo: text_demo.o
	$(CC) -Wall $^ -o $@ -lhpdf -lm

cmarkpdf: cmarkpdf.o pdf.o
	$(CC) $^ -o $@ -lhpdf -lcmark

clean:
	-rm *.pdf *.o cmarkpdf $(DEMOS)
