CC=cc
CFLAGS?=-O2 -Wall
LDFLAGS?=
LIBS=-lcurl

.PHONY: glitch clean install

OBJS = glitch.o img.o

glitch: $(OBJS) colors.h shape.h
	$(CC) $(CFLAGS) -o glitch $(OBJS) -lm $(LIBS) $(LDFLAGS)

glitch.o: glitch.c colors.h shape.h img.h
img.o: img.c img.h

lean: CFLAGS+=-O2 -pipe -march=native -fno-plt -Wall
lean: LDFLAGS+=-s
lean: glitch

minimal: CFLAGS+=-DMINIMAL_BUILD -Wall
minimal: LIBS=
minimal: glitch

clean:
	rm -f glitch $(OBJS) colors.h shape.h
