PLATFORM      = $(shell uname -s)
SHORTPLATFORM = $(shell (X=`uname -s`; echo $${X:0:10}))

BUILDPATH ?= build

# platform specific settings:
ifeq ($(PLATFORM), Linux)
  STATIC_LDFLAGS = -rs
  DYNAMIC_LDFLAGS = -shared -lm -ldl
  DYNAMIC_LIB = $(BUILDPATH)/libumka.so
  EXECUTABLE_DEPS = -lm -ldl
  RANLIB = ar
else
ifeq ($(PLATFORM), Darwin)
  STATIC_LDFLAGS =
  DYNAMIC_LDFLAGS = -shared
  DYNAMIC_LIB = $(BUILDPATH)/libumka.dylib
  EXECUTABLE_DEPS =
  RANLIB = libtool -static -o
else
ifeq ($(SHORTPLATFORM), MINGW64_NT)
  STATIC_LDFLAGS = -rs
  DYNAMIC_LDFLAGS = -shared -lm
  DYNAMIC_LIB = $(BUILDPATH)/libumka.dll
  EXECUTABLE_DEPS = -lm
  RANLIB = ar
endif
endif
endif

# for all platforms same:
STATIC_LIB = $(BUILDPATH)/libumka.a
CFLAGS = -s -fPIC -O3 -Wall -Wno-format-security -fno-strict-aliasing -DUMKA_EXT_LIBS
STATIC_CFLAGS  = $(CFLAGS) -DUMKA_STATIC
DYNAMIC_CFLAGS = $(CFLAGS) -DUMKA_BUILD  -fvisibility=hidden

SRCS           = $(filter-out src/umka.c,$(wildcard src/*.c))
OBJS_STATIC    = $(sort $(SRCS:.c=_static.o))
OBJS_DYNAMIC   = $(sort $(SRCS:.c=_dynamic.o))
EXECUTABLE_OBJ = src/umka_static.o
EXECUTABLE     = $(BUILDPATH)/umka

.PHONY: all path clean static dynamic
all: path $(EXECUTABLE) $(STATIC_LIB) $(DYNAMIC_LIB)

static:  path $(STATIC_LIB)
dynamic: path $(DYNAMIC_LIB)

clean:
	$(RM) $(BUILDPATH) -r
	$(RM) $(wildcard src/*.o)
	$(RM) src/umka_runtime_src.h

path:
	@mkdir $(BUILDPATH) -p
	@cp import_embed/umka_runtime_src.h src/

$(STATIC_LIB): $(OBJS_STATIC)
	@echo AR $@
	@$(RANLIB) $(STATIC_LDFLAGS) $(STATIC_LIB) $^

$(DYNAMIC_LIB): $(OBJS_DYNAMIC)
	@echo LD $@
	@$(CC) $(DYNAMIC_LDFLAGS) $(DYNAMIC_CFLAGS) -o $(DYNAMIC_LIB) $^

$(EXECUTABLE): $(EXECUTABLE_OBJ) $(STATIC_LIB)
	@echo LD $@
	@$(CC) $(FLAGS) -o $(EXECUTABLE) $^ $(EXECUTABLE_DEPS)

%_static.o: %.c
	@echo CC $@
	@$(CC) $(STATIC_CFLAGS) -c -o $@ $^

%_dynamic.o: %.c
	@echo CC $@
	@$(CC) $(DYNAMIC_CFLAGS) -c -o $@ $^

