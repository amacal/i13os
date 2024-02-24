ARCH = x86_64
EFIINC = /usr/include/efi
EFIINCS = -I$(EFIINC) -I$(EFIINC)/$(ARCH) -I$(EFIINC)/protocol
LIB = /usr/lib
EFILIB = /usr/lib
CFLAGS = $(EFIINCS) -fno-stack-protector -fpic -fshort-wchar -mno-red-zone -Wall
LDFLAGS = -nostdlib -znocombreloc -T $(EFILIB)/elf_$(ARCH)_efi.lds -shared -Bsymbolic -L$(EFILIB) -L$(LIB) $(EFILIB)/crt0-efi-$(ARCH).o

all: efi

main.so: src/main.c
	$(CC) $(CFLAGS) -c -o src/main.o src/main.c
	ld $(LDFLAGS) -o src/main.so src/main.o -lefi -lgnuefi

efi: main.so
	mkdir -p efi/boot
	objcopy -j .text -j .sdata -j .data -j .dynamic -j .dynsym  -j .rel -j .rela -j .reloc --target=efi-app-$(ARCH) src/main.so efi/boot/bootx64.efi

run: efi
	qemu-system-x86_64 -nographic -drive format=raw,file=fat:rw:. -m 128 -bios /usr/share/ovmf/OVMF.fd

clean:
	rm -rf efi/
	rm -f src/*.o *.so *.efi
