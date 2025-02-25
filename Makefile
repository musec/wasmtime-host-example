.PHONY: clean
.SUFFIXES: .wasm

CFLAGS=		-I /usr/local/include
LDFLAGS=	-L /usr/local/lib -lwasmtime

WASM_CC=	clang20
WASM_CFLAGS=	-target wasm32 -nostdlib -Wl,--no-entry -Wl,--export-all

all: host example.wasm

clean:
	rm -f host *.core *.wasm

host: host.c

.c.wasm:
	$(WASM_CC) $(WASM_CFLAGS) $< -o $@
