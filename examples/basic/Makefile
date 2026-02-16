
TARGET = basic.elf
OBJS = example.o mpeg.o romdisk.o
KOS_ROMDISK_DIR = ../../romdisk
KOS_CFLAGS += -g -I../..

all: rm-elf $(TARGET)

include $(KOS_BASE)/Makefile.rules

clean: rm-elf
	-rm -f $(OBJS) romdisk.img

rm-elf:
	-rm -f $(TARGET)

mpeg.o: ../../mpeg.c
	kos-cc $(KOS_CFLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	kos-cc -o $(TARGET) $(OBJS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

dist: $(TARGET)
	-rm -f $(OBJS)
	$(KOS_STRIP) $(TARGET)
