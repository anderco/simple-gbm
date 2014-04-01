PKGS = "libdrm gbm wayland-client"
CFLAGS = $(shell pkg-config --cflags $(PKGS)) -Wall -g
LDFLAGS = $(shell pkg-config --libs-only-L $(PKGS))
LDLIBS = $(shell pkg-config --libs-only-l $(PKGS))

all: simple-gbm

simple-gbm: simple-gbm.o wayland-drm-protocol.o

clean:
	rm simple-gbm *.o
