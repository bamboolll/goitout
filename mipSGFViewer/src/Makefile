CC=gcc
CFLAGS=-c -Wall
LDFLAGS=
SOURCES=mipgo.c mboard.c mboardlib.c mhash.c msgf_utils.c msgftree.c mwinsocket.c mrandom.c mprintutils.c msgfnode.c mgg_utils.c msgffile.c mhandicap.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=mipgo.out

all: $(SOURCES) $(EXECUTABLE)
	
$(EXECUTABLE): $(OBJECTS) 
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@
clean:
	-rm -f *.o

