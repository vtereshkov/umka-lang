PLATFORM      = $(shell uname -s)
SHORTPLATFORM = $(shell (X=`uname -s`; echo $${X:0:10}))

BUILD_PATH ?= build

# platform specific settings:
ifeq ($(PLATFORM), Linux)
	LDFLAGS = -lm -ldl
	RANLIB = ar -crs
else
ifeq ($(PLATFORM), Darwin)
	LDFLAGS =
	RANLIB = libtool -static -o
else
ifeq ($(SHORTPLATFORM), MINGW64_NT)
	LDFLAGS = -lm
	RANLIB = ar -crs
endif
endif
endif

# identical for all platforms:
UMKA_LIB_STATIC  = $(BUILD_PATH)/libumka.a
UMKA_LIB_DYNAMIC = $(BUILD_PATH)/libumka.so
UMKA_EXE = $(BUILD_PATH)/umka

CFLAGS = -s -fPIC -O3 -Wall -Wno-format-security -fno-strict-aliasing -DUMKA_EXT_LIBS
STATIC_CFLAGS  = $(CFLAGS) -DUMKA_STATIC
DYNAMIC_CFLAGS = $(CFLAGS) -DUMKA_BUILD  -shared -fvisibility=hidden

SRCS = $(filter-out src/umka.c,$(wildcard src/*.c))
OBJS_STATIC    = $(sort $(SRCS:src/%.c=obj/%_static.o))
OBJS_DYNAMIC   = $(sort $(SRCS:src/%.c=obj/%_dynamic.o))

APIS = src/umka_api.h
OBJS_EXE = obj/umka_static.o

.PHONY: all path clean
all: path $(UMKA_EXE) $(UMKA_LIB_STATIC) $(UMKA_LIB_DYNAMIC)

static:  path $(UMKA_LIB_STATIC)
dynamic: path $(UMKA_LIB_DYNAMIC)
exe:     path $(UMKA_EXE)

clean:
	$(RM) $(BUILD_PATH) obj -r
	$(RM) src/umka_runtime_src.h

path:
	@mkdir -p -- obj $(BUILD_PATH)/include
	@cp import_embed/umka_runtime_src.h src/

$(UMKA_LIB_STATIC): $(OBJS_STATIC)
	@echo AR $@
	@$(RANLIB) $(UMKA_LIB_STATIC) $^
	@cp $(APIS) $(BUILD_PATH)/include/

$(UMKA_LIB_DYNAMIC): $(OBJS_DYNAMIC)
	@echo LD $@
	@$(CC) $(DYNAMIC_CFLAGS) -o $(UMKA_LIB_DYNAMIC) $^ $(LDFLAGS)
	@cp $(APIS) $(BUILD_PATH)/include/

$(UMKA_EXE): $(OBJS_EXE) $(UMKA_LIB_STATIC)
	@echo LD $@
	@$(CC) $(STATIC_CFLAGS) -o $(UMKA_EXE) $^ $(LDFLAGS)

obj/%_static.o: src/%.c
	@echo CC $@
	@$(CC) $(STATIC_CFLAGS) -o $@ -c $^

obj/%_dynamic.o: src/%.c
	@echo CC $@
	@$(CC) $(DYNAMIC_CFLAGS) -o $@ -c $^

