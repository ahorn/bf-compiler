CC = gcc
CFLAGS = -g -Wall

CFILES = bfc.c
TARG = bfc

all: $(TARG)

$(TARG): $(CFILES)
	$(CC) $(CFLAGS) -o $(TARG) $(CFILES) 

install: $(TARG) 
	install -m 755 $(TARG) /usr/local/bin

uninstall:
	rm -rf /usr/local/bin/$(TARG)

CLEANFILES+=$(TARG)
clean:
	rm -f *.o $(CLEANFILES)

.PHONY: all
.PHONY: clean
