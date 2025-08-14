CC := gcc
CFLAGS := -Wall -Wextra -fPIC -Wno-frame-address -Wno-unused-variable -std=c99
LDFLAGS :=

# Detect host architecture once
UNAME_M := $(shell uname -m)

# ARCH override (32 or 64)
ifeq ($(ARCH),32)
    ifneq (,$(filter x86_64 amd64,$(UNAME_M)))
        # Build 32-bit on x86_64 host
        CFLAGS  += -m32
        LDFLAGS += -m32

    else ifneq (,$(filter i386 i486 i586 i686,$(UNAME_M)))
        # Already 32-bit x86 — nothing to add

    else ifneq (,$(filter armv6% armv7%,$(UNAME_M)))
        # Already 32-bit ARM — nothing to add

    else ifneq (,$(filter aarch64 arm64,$(UNAME_M)))
        # 64-bit ARM → compile 32-bit ARM (armhf) compatible with Raspberry Pi
        CC     := arm-linux-gnueabihf-gcc
        CFLAGS += -march=armv7-a -mfpu=vfpv3 -mfloat-abi=hard
    endif

else ifeq ($(ARCH),64)
    ifneq (,$(filter x86_64 amd64,$(UNAME_M)))
        CFLAGS  += -m64
        LDFLAGS += -m64

    else ifneq (,$(filter aarch64 arm64,$(UNAME_M)))
        # Already 64-bit ARM — nothing to add

    else ifneq (,$(filter armv6% armv7%,$(UNAME_M)))
        # 32-bit ARM → force 64-bit
        CFLAGS += -march=armv8-a
    endif
endif

#Add pthreads if compiling under Linux
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
    CFLAGS  += -pthread
    LDFLAGS += -pthread
endif


# If DEBUG=1 is passed to make, add debug flags
ifeq ($(DEBUG),1)
    CFLAGS += -g -O0 -DDEBUG
else
    CFLAGS += -O3
endif

# Targets
all: libgc.a libgc.so gc

# Object file for GC library
gc.o: gc.c gc.h
	$(CC) $(CFLAGS) -c gc.c -o $@

# Static library
libgc.a: gc.o
	ar rcs $@ $^

# Shared library
libgc.so: gc.o
	$(CC) -shared -o $@ $^

# Build gc_test executable linked against static library
gc: gc_test.o libgc.a
	$(CC) $(CFLAGS) gc_test.o ./libgc.a -o $@

# Object file for test
gc_test.o: gc_test.c gc.h
	$(CC) $(CFLAGS) -c gc_test.c -o $@

# Clean up build artifacts
clean:
	rm -f *.o *.a *.so gc

.PHONY: all clean
