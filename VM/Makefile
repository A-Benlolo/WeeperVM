CC = gcc
CFLAGS = -nostdlib -masm=intel -Wno-shift-overflow -Ilib -Os
LDFLAGS = -Tminimal.ld -Wl,--build-id=none
OBJDIR = obj
SRCDIR = src
DISTDIR = dist

MAIN_SRC = WeeperVM.c
MAIN_OBJ = $(OBJDIR)/WeeperVM.o

SRC_FILES := $(wildcard $(SRCDIR)/*.c)
OBJ_FILES := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRC_FILES)) $(MAIN_OBJ)


TARGET = $(DISTDIR)/WeeperVM

all: $(TARGET)


$(TARGET): $(OBJ_FILES) $(MAIN_OBJ) woede.bin
	@mkdir -p $(DISTDIR)
	$(CC) $(CFLAGS) $(OBJ_FILES) $(LDFLAGS) -o $@
	strip --strip-all $(TARGET)
	cat woede.bin >> $(TARGET)


$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@


$(MAIN_OBJ): $(MAIN_SRC)
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@


clean:
	rm -rf $(OBJDIR) $(DISTDIR)

force:
	$(MAKE) clean
	$(MAKE) all

.PHONY: all clean force
