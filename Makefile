ARCH = x86_64
EFIINC = /usr/include/efi
EFIINCS = -I$(EFIINC) -I$(EFIINC)/$(ARCH) -I$(EFIINC)/protocol
LIB = /usr/lib
EFILIB = /usr/lib
CFLAGS = $(EFIINCS) -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -Wall
LDFLAGS = -nostdlib -znocombreloc -T $(EFILIB)/elf_$(ARCH)_efi.lds -shared -Bsymbolic -L$(EFILIB) -L$(LIB) $(EFILIB)/crt0-efi-$(ARCH).o

all: bootloader.efi kernel.bin

bootloader.efi: src/bootloader.c
	$(CC) $(CFLAGS) -S -o src/bootloader.s src/bootloader.c
	$(CC) $(CFLAGS) -c -o src/bootloader.o src/bootloader.c
	ld $(LDFLAGS) -o src/bootloader.so src/bootloader.o -lefi -lgnuefi
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym  -j .rel -j .rela -j .reloc --target=efi-app-$(ARCH) src/bootloader.so src/bootloader.efi

kernel.bin: src/kernel.c
	$(CC) -S -O3 -o src/kernel.s src/kernel.c
	$(CC) -ffreestanding -fPIC -c src/kernel.c -o src/kernel.o
	ld -nostdlib -pie src/kernel.o -o src/kernel.elf
	objcopy -O binary -j .text src/kernel.elf src/kernel.bin

efi: bootloader.efi kernel.bin
	mkdir -p efi/boot
	cp src/kernel.bin efi/boot/kernel.bin
	cp src/bootloader.efi efi/boot/bootx64.efi

run: efi
	qemu-system-x86_64 -nographic -drive format=raw,file=fat:rw:. -m 1024 -bios /usr/share/ovmf/OVMF.fd

clean:
	rm -rf efi/
	rm -f src/*.o *.so *.efi
