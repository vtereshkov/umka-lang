PLATFORM      = $(shell uname -s)
SHORTPLATFORM = $(shell (X=`uname -s`; echo $${X:0:10}))

BUILD_PATH ?= build

# platform specific settings:
ifeq ($(PLATFORM), Linux)
	LDFLAGS = -rs
	EXE_DEPS = -lm -ldl
	RANLIB = ar -c
else
ifeq ($(PLATFORM), Darwin)
	LDFLAGS =
	EXE_DEPS =
	RANLIB = libtool -static -o
else
ifeq ($(SHORTPLATFORM), MINGW64_NT)
	LDFLAGS = -rs
	EXE_DEPS = -lm
	RANLIB = ar -c
endif
endif
endif

# identical for all platforms:
UMKA_LIB = $(BUILD_PATH)/libumka.a
UMKA_EXE = $(BUILD_PATH)/umka
CFLAGS = -s -fPIC -O3 -Wall -Wno-format-security \
		 -fno-strict-aliasing -DUMKA_EXT_LIBS -DUMKA_STATIC

SRCS = $(filter-out src/umka.c,$(wildcard src/*.c))
OBJS = $(sort $(SRCS:src/%.c=obj/%.o))
APIS = src/umka_api.h
OBJS_EXE = obj/umka.o

.PHONY: all path clean
all: path $(UMKA_EXE) $(UMKA_LIB)

clean:
	$(RM) $(BUILD_PATH) obj -r
	$(RM) src/umka_runtime_src.h

path:
	@mkdir -p -- obj $(BUILD_PATH)/include
	@cp import_embed/umka_runtime_src.h src/

$(UMKA_LIB): $(OBJS)
	@echo AR $@
	@$(RANLIB) $(LDFLAGS) $(UMKA_LIB) $^
	@cp $(APIS) $(BUILD_PATH)/include/

$(UMKA_EXE): $(OBJS_EXE) $(UMKA_LIB)
	@echo LD $@
	@$(CC) $(CFLAGS) -o $(UMKA_EXE) $^ $(EXE_DEPS)

obj/%.o: src/%.c
	@echo CC $@
	@$(CC) $(CFLAGS) -o $@ -c $^

