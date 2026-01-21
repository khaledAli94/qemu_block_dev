CROSS_COMPILE ?=arm-none-linux-gnueabihf-


CC	= $(CROSS_COMPILE)gcc
LD	= $(CROSS_COMPILE)ld
OC	= $(CROSS_COMPILE)objcopy
SZ	= $(CROSS_COMPILE)size
OD	= $(CROSS_COMPILE)objdump
NM 	= $(CROSS_COMPILE)nm
RE	= $(CROSS_COMPILE)readelf

TARGET	=bootloader

# Compilation and linking flags
DEFINES = -D__ARM32_ARCH__=7 -D__CORTEX_A9__ -D__ARM32_NEON__ -D__BAREMETAL__
MCFLAGS = -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -marm -mno-thumb-interwork -mno-unaligned-access -fverbose-asm -fstack-usage -Wstack-usage=1024

# build flags
CFLAGS 	+= $(DEFINES) $(MCFLAGS) -g3 -ggdb -Wall -O0 -ffunction-sections -fdata-sections -ffreestanding -std=gnu99 -fomit-frame-pointer -fdump-rtl-expand 
ASFLAGS	+= $(DEFINES) -x assembler-with-cpp $(MCFLAGS) -g3 -ggdb -Wall -O0 -ffunction-sections -fdata-sections -ffreestanding -std=gnu99 -fomit-frame-pointer -fdump-rtl-expand

# nano.specs automatically adds -lc_nano
LDFLAGS	= -T ./memlayout.ld -Wl,-Map=./$(TARGET).map -Wl,--gc-sections -Wl,--cref -z noexecstack  -Wl,--no-warn-rwx-segments -nostdlib -nostartfiles #-static # static for QemuDebug


export CC LD ASFLAGS CFLAGS INCDIRS


H_DIRS := $(sort $(dir $(shell find . -type f -name '*.h')))
INCDIRS  := $(strip $(patsubst %, -I%, $(H_DIRS)))


SRC		= $(shell find . -type d) 
SFILES	:= $(foreach dir, $(SRC), $(wildcard $(dir)/*.S))
CFILES	:= $(foreach dir, $(SRC), $(wildcard $(dir)/*.c))


DEPS	:= $(SFILES:.S=.o.d) $(CFILES:.c=.o.d)

LIBS	:= #-lgcc #-lc #-lm  #-lc_nano #$(shell $(CC) --print-libgcc-file-name) #$(shell $(CC) -print-file-name=libc_nano.a) 

SOBJS	:= $(patsubst %.S, %.o, $(SFILES))
COBJS	:= $(patsubst %.c, %.o, $(CFILES))
OBJS := $(SOBJS) $(COBJS)


# Create the target binary
all: $(OBJS) link
	
link: $(OBJS)
	@echo 
	@echo [LD] $(notdir $(OBJS))
	@$(CC) $(LDFLAGS) $^ -o $(TARGET).elf $(LIBS)
	@echo 
	@echo [OC] Objcopying..
	@$(OC) -O binary $(TARGET).elf $(TARGET).bin
	@$(OC) $(TARGET).elf $(TARGET).sym --only-keep-debug --strip-all
	
	@echo [SZ] Listing size..  
	@$(SZ) $(TARGET).elf -G	    #-A|-B|-G



$(SOBJS): %.o: %.S 
	@echo [AS] $(notdir $<)
	@$(CC) $(ASFLAGS) $(INCDIRS) -c $< -o $@  -MD -MP -MF $@.d


$(COBJS): %.o: %.c
	@echo [CC] $(notdir $<)
	@$(CC) $(CFLAGS) $(INCDIRS) -c $< -o $@  -MD -MP -MF $@.d



# Clean build artifacts
clean:
	@echo cleaning up.. 
	@rm -f -r $(shell find . -name "*.d" -o -name "*.map" -o -name "*.bin" -o -name "*.o" -o -name "*.su" -o -name "*.expand" -o -name "*.sym" -o -name "*.elf" -o -name "*.map" )


print:
	@echo sfiles:$(SFILES)
	@echo
	@echo cfiles:$(CFILES)
	@echo
	@echo objs:$(OBJS)
	@echo
	@echo INCDIRS:$(INCDIRS)


dump: all
	@echo [OD] Dumping..
	@$(OD) $(TARGET) -D --disassembler-color=on --visualize-jumps=color




lsym: $(SOBJS) $(COBJS)
	@$(NM) $(SOBJS) $(COBJS) -g -S   #-g -a -S -l -W 

readelf: link
	@$(RE) ./$(TARGET).elf -S  #-S|-a
	@$(OD) ./$(TARGET).elf -h 
	

.PHONY: all $(SOBJS) $(COBJS) link clean
