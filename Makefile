# CC = gcc
CFLAGS = -Wall -O3 -std=c99 -I. -Iinclude
LDFLAGS = -Llib -ljpeg -lpcreposix -lpcre

LIB_DIR = lib

BIN = f5ar
LIB = libf5ar
LIB_CMD = libf5arcmd

FILES = $(BIN) $(LIB).a $(LIB_CMD).a
all: $(BIN) $(LIB) $(LIB_CMD) _done

# Private targets
_f5ar.o: md5.o f5ar.o
_f5ar_cmd.o: _f5ar.o f5ar_utils.o f5ar_cmd.o

# Public targets
$(LIB): _f5ar.o
	$(AR) rcs $(LIB).a f5ar.o
$(BIN): _f5ar_cmd.o
	$(CC) $(CFLAGS) -o $(BIN) f5ar_cmd.o f5ar.o md5.o $(LDFLAGS)
$(LIB_CMD): _f5ar_cmd.o
	$(AR) rcs $(LIB_CMD).a f5ar_cmd.o

_done:
	$(info Compilation completed successfully)

# Local libjpeg build
libjpeg:
	mkdir -p lib && cd lib; \
	if [ ! -d libjpeg-turbo ]; then git clone https://github.com/libjpeg-turbo/libjpeg-turbo; fi; \
	cd libjpeg-turbo && \
	mkdir -p build && cd build && \
	cmake .. && make jpeg-static && \
	cp libjpeg.a ../../;

PCRE_VER = 8.43
PCRE = pcre-$(PCRE_VER)
pcre:
	mkdir -p lib && cd lib; \
	if [ ! -d $(PCRE) ]; then \
	    wget https://ftp.pcre.org/pub/pcre/$(PCRE).tar.bz2 && tar -xjvf $(PCRE).tar.bz2 && rm $(PCRE).tar.bz2; \
	fi; \
	cd $(PCRE) && mkdir -p build && cd build && \
	cmake .. && make pcreposix && \
	cp libpcreposix.a ../../ && cp libpcre.a ../../;

clean:
	rm -f *.o *.a $(BIN)

install:
	sudo cp $(BIN) /usr/bin/
	sudo cp $(LIB).a /usr/lib/

remove:
	sudo rm /usr/bin/$(BIN)
	sudo rm /usr/lib/$(LIB).a

.PHONY: all libjpeg clean install remove _done _f5ar.o _f5ar_cmd.o