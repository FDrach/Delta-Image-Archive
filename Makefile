CC := gcc
PKG_CFLAGS := $(shell pkg-config --cflags gtk+-3.0 json-glib-1.0)
PKG_LIBS := $(shell pkg-config --libs gtk+-3.0 json-glib-1.0)

CFLAGS ?= -O2
CPPFLAGS ?=
CFLAGS += -Isrc $(PKG_CFLAGS)
LDLIBS += $(PKG_LIBS) -lzip

SRCS := main.c \
        src/app.c \
        src/ui.c \
        src/render.c \
        src/io.c
OBJS := $(SRCS:.c=.o)

.PHONY: all clean

all: composite_browser2

composite_browser2: $(OBJS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(OBJS) $(LDLIBS)

clean:
	rm -f $(OBJS) composite_browser2
