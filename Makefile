CC      = gcc
CFLAGS  = -Wall -Wextra -g -pthread
LDFLAGS = -pthread -lm -lrt

TARGETS = init g_reader motor_control supervisor logger ui

all: $(TARGETS)

run : 
	./init
init:          init.c          common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

g_reader:      g_reader.c      common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

motor_control: motor_control.c common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

supervisor:    supervisor.c    common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

logger:        logger.c        common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

ui:            ui.c            common.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGETS) printer.log

.PHONY: all clean
