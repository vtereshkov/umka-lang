platform := $(shell uname -s)
shortplatform := $(shell (X=`uname -s`; echo $${X:0:10}))
ifeq ($(platform), Linux)
  LPATH = LD_LIBRARY_PATH
  STATIC_CFLAGS = -fPIC -O3 -Wall -Wno-format-security -DUMKA_STATIC
  DYNAMIC_CFLAGS = -fPIC -O3 -Wall -fvisibility=hidden -DUMKA_BUILD
  STATIC_LDFLAGS = -rs 
  DYNAMIC_LDFLAGS = -shared -lm
  STATIC_LIB = libumka.a
  DYNAMIC_LIB = libumka.so
  EXECUTABLE_DEPS = -lm
  RANLIB = ar
else
ifeq ($(platform), Darwin)
  LPATH = DYLD_LIBRARY_PATH
  STATIC_CFLAGS = -fPIC -O3 -Wall -Wno-format-security -DUMKA_STATIC
  DYNAMIC_CFLAGS = -fPIC -O3 -Wall -fvisibility=hidden -DUMKA_BUILD
  STATIC_LDFLAGS = 
  DYNAMIC_LDFLAGS = -shared
  STATIC_LIB = libumka.a
  DYNAMIC_LIB = libumka.dylib
  EXECUTABLE_DEPS = 
  RANLIB = libtool -static -o  
else
ifeq ($(shortplatform), MINGW64_NT)
  LPATH = PATH
  STATIC_CFLAGS = -fPIC -O3 -Wall -Wno-format-security -DUMKA_STATIC
  DYNAMIC_CFLAGS = -fPIC -O3 -Wall -fvisibility=hidden -DUMKA_BUILD
  STATIC_LDFLAGS = -rs 
  DYNAMIC_LDFLAGS = -shared -lm
  STATIC_LIB = libumka.a
  DYNAMIC_LIB = libumka.so
  EXECUTABLE_DEPS = -lm
  RANLIB = ar
endif
endif
endif

ifndef LPATH
$(warning Unrecognized kernel name ${platform} -- Unable to detect setting for LPATH)
endif

STATIC_LIB_OBJ = src/umka_api_static.o src/umka_common_static.o src/umka_compiler_static.o src/umka_const_static.o src/umka_decl_static.o src/umka_expr_static.o src/umka_gen_static.o src/umka_ident_static.o src/umka_lexer_static.o src/umka_runtime_static.o src/umka_stmt_static.o src/umka_types_static.o src/umka_vm_static.o
DYNAMIC_LIB_OBJ = src/umka_api_dynamic.o src/umka_common_dynamic.o src/umka_compiler_dynamic.o src/umka_const_dynamic.o src/umka_decl_dynamic.o src/umka_expr_dynamic.o src/umka_gen_dynamic.o src/umka_ident_dynamic.o src/umka_lexer_dynamic.o src/umka_runtime_dynamic.o src/umka_stmt_dynamic.o src/umka_types_dynamic.o src/umka_vm_dynamic.o
EXECUTABLE_OBJ = src/umka_static.o
EXECUTABLE = umka

.PHONY: all clean
all: $(EXECUTABLE) $(STATIC_LIB) $(DYNAMIC_LIB)

clean:
	rm -f $(EXECUTABLE) $(STATIC_LIB) $(DYNAMIC_LIB) 
	rm -f src/*.o

$(STATIC_LIB): $(STATIC_LIB_OBJ)
	# Build static archive 
	$(RANLIB) $(STATIC_LDFLAGS) $(STATIC_LIB) $^

$(DYNAMIC_LIB): $(DYNAMIC_LIB_OBJ)
	# Build shared library
	$(CC) $(DYNAMIC_LDFLAGS) -o $(DYNAMIC_LIB) $^

$(EXECUTABLE): $(EXECUTABLE_OBJ) $(STATIC_LIB) 
	# Build executable 
	$(CC) $(STATIC_CFLAGS) -o $(EXECUTABLE) $^ $(EXECUTABLE_DEPS)

src/%_static.o: src/%.c
	$(CC) $(STATIC_CFLAGS) -c -o $@ $^


src/%_dynamic.o: src/%.c
	$(CC) $(DYNAMIC_CFLAGS) -c -o $@ $^
