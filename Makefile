.PHONY: all clean

PLATFORM ?= $(shell uname -s)

BUILD_PATH ?= build
OBJ_PATH   ?= obj

# installation prefix
PREFIX ?= /usr/local
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
BINDIR ?= $(PREFIX)/bin

# platform specific settings:
ifeq ($(PLATFORM), Linux)
	LDFLAGS = -lm -ldl
	RANLIB = ar -crs
else ifeq ($(PLATFORM), Darwin)
	LDFLAGS =
	RANLIB = libtool -static -o
else ifneq ($(findstring MINGW64_NT,$(PLATFORM)),)
	LDFLAGS = -lm
	RANLIB = ar -crs
endif

# identical for all platforms:
UMKA_LIB_STATIC  = $(BUILD_PATH)/libumka.a
UMKA_LIB_DYNAMIC = $(BUILD_PATH)/libumka.so
UMKA_EXE = $(BUILD_PATH)/umka

CFLAGS = -s -fPIC -O3 -Wall -Wno-format-security -malign-double -fno-strict-aliasing -DUMKA_EXT_LIBS
STATIC_CFLAGS  = $(CFLAGS) -DUMKA_STATIC
DYNAMIC_CFLAGS = $(CFLAGS) -DUMKA_BUILD  -shared -fvisibility=hidden

SRCS = $(filter-out src/umka.c,$(wildcard src/*.c))
OBJS_STATIC    = $(sort $(SRCS:src/%.c=$(OBJ_PATH)/%_static.o))
OBJS_DYNAMIC   = $(sort $(SRCS:src/%.c=$(OBJ_PATH)/%_dynamic.o))

APIS = src/umka_api.h
OBJS_EXE = $(OBJ_PATH)/umka_static.o

all:     $(UMKA_EXE) $(UMKA_LIB_STATIC) $(UMKA_LIB_DYNAMIC)
static:  $(UMKA_LIB_STATIC)
dynamic: $(UMKA_LIB_DYNAMIC)
exe:     $(UMKA_EXE)

clean:
	$(RM) $(BUILD_PATH) $(OBJ_PATH) -r

install: all
	@echo "Installing to the following directories:"
	@echo "  Libraries: $(DESTDIR)$(LIBDIR)"
	@echo "  Includes:  $(DESTDIR)$(INCLUDEDIR)"
	@echo "  Binaries:  $(DESTDIR)$(BINDIR)"
	@mkdir -p -- $(DESTDIR)$(LIBDIR)
	@mkdir -p -- $(DESTDIR)$(BINDIR)
	@mkdir -p -- $(DESTDIR)$(INCLUDEDIR)
	@echo "Copying files..."
	@cp $(UMKA_LIB_STATIC) $(DESTDIR)$(LIBDIR)/
	@cp $(UMKA_LIB_DYNAMIC) $(DESTDIR)$(LIBDIR)/
	@cp $(UMKA_EXE) $(DESTDIR)$(BINDIR)/
	@cp $(APIS) $(DESTDIR)$(INCLUDEDIR)/
	@echo "Installation complete!"

uninstall:
	@echo "Uninstalling following files:"
	@echo "  $(DESTDIR)$(LIBDIR)/libumka.a"
	@echo "  $(DESTDIR)$(LIBDIR)/libumka.so"
	@echo "  $(DESTDIR)$(BINDIR)/umka"
	@echo "  $(DESTDIR)$(INCLUDEDIR)/umka_api.h"
	@rm -f -- $(DESTDIR)$(LIBDIR)/libumka.a
	@rm -f -- $(DESTDIR)$(LIBDIR)/libumka.so
	@rm -f -- $(DESTDIR)$(BINDIR)/umka
	@rm -f -- $(DESTDIR)$(INCLUDEDIR)/umka_api.h
	@echo "Uninstallation complete!"

$(UMKA_LIB_STATIC): $(OBJS_STATIC)
	@echo AR $@
	@mkdir -p -- $(BUILD_PATH)/include/
	@$(RANLIB) $(UMKA_LIB_STATIC) $^
	@cp $(APIS) $(BUILD_PATH)/include/

$(UMKA_LIB_DYNAMIC): $(OBJS_DYNAMIC)
	@echo LD $@
	@mkdir -p -- $(BUILD_PATH)/include/
	@$(CC) $(DYNAMIC_CFLAGS) -o $(UMKA_LIB_DYNAMIC) $^ $(LDFLAGS)
	@cp $(APIS) $(BUILD_PATH)/include/

$(UMKA_EXE): $(OBJS_EXE) $(UMKA_LIB_STATIC)
	@echo LD $@
	@mkdir -p -- $(dir $@)
	@$(CC) $(STATIC_CFLAGS) -o $(UMKA_EXE) $^ $(LDFLAGS)

$(OBJ_PATH)/%_static.o: src/%.c
	@echo CC $@
	@mkdir -p -- $(dir $@)
	@$(CC) $(STATIC_CFLAGS) -o $@ -c $^

$(OBJ_PATH)/%_dynamic.o: src/%.c
	@echo CC $@
	@mkdir -p -- $(dir $@)
	@$(CC) $(DYNAMIC_CFLAGS) -o $@ -c $^
