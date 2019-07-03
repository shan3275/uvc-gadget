CROSS_COMPILE	?=
ARCH		?= arm
KERNEL_DIR	?= /usr

CC		:= $(CROSS_COMPILE)gcc
KERNEL_INCLUDE	:= -I$(KERNEL_DIR)/include -I$(KERNEL_DIR)/arch/$(ARCH)/include
CFLAGS += -Wall -g $(KERNEL_INCLUDE) -I /usr/local/include
ECFLAGS = -Wall -g $(KERNEL_INCLUDE) -I /usr/local/include
LDFLAGS		+= -g
ELDFLAGS += -L/usr/local/lib
LDLIBS =  -lavformat -lavcodec -lswscale -lswresample -lavutil -lavfilter -lavdevice
LDLIBS +=  -lm -pthread

PROG    = uvc

SOURCES = $(wildcard *.c)
OBJS   ?= $(patsubst %.c,%.o,$(SOURCES))
MOD    ?= $(PROG)

all: $(PROG)

%.o: %.c
	$(CC) $(CFLAGS) $(ECFLAGS) -c $< -o $@


$(PROG): $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS) $(ELDFLAGS) $(LDLIBS)

clean:
	rm -rf *.o
	rm -rf $(PROG)