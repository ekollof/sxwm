CC      ?= clang
CFLAGS  ?= -std=c99 -Werror -ansi -pedantic -std=c99 -Wall -Wextra -O3 -Isrc -g -I/usr/X11R6/include
LDFLAGS ?= -lX11 -lXinerama -lXcursor -L/usr/X11R6/lib

PREFIX  ?= /usr/local
BIN     := sxwm
SRC_DIR := src
OBJ_DIR := build
SRC     := $(wildcard $(SRC_DIR)/*.c)
OBJ     := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRC))

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

clean:
	rm -rf $(OBJ_DIR) $(BIN)

install: all
	@echo "Installing $(BIN) to $(DESTDIR)$(PREFIX)/bin..."
	@mkdir -p $(DESTDIR)$(PREFIX)/bin
	@install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	@echo "Installation complete."

uninstall:
	@echo "Uninstalling $(BIN) from $(DESTDIR)$(PREFIX)/bin..."
	@rm -f $(DESTDIR)$(PREFIX)/bin/$(BIN)
	@echo "Uninstallation complete."

.PHONY: all clean install uninstall
