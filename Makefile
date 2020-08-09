.POSIX:

SRC = src
CFLAGS = -fPIC -O3 -Wall -Wno-format-security
LDFLAGS = -static-libgcc

BIN_OBJ = src/umka.o
LIB_OBJ = src/umka_api.o src/umka_common.o src/umka_compiler.o src/umka_const.o src/umka_decl.o src/umka_expr.o src/umka_gen.o src/umka_ident.o src/umka_lexer.o src/umka_runtime.o src/umka_stmt.o src/umka_types.o src/umka_vm.o

.PHONY: all clean
all: umka libumka.so
clean:
	rm -f umka libumka.so
	rm -f src/*.o

umka: $(BIN_OBJ) $(LIB_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ -lm

libumka.so: $(LIB_OBJ)
	$(CC) $(LDFLAGS) -shared -fPIC -o libumka.so $^ -lm

src/%.o: src/%.c
