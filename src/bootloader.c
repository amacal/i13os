#include <efi.h>
#include <efilib.h>

EFI_STATUS print_status(EFI_STATUS status, CHAR16 *context, UINTN delay)
{
  if (EFI_ERROR(status))
  {
    Print(L"Something went wrong: %d, %s\n", status, context);
  }

  if (delay)
  {
    uefi_call_wrapper(BS->Stall, 1, delay * 1000000);
  }

  return status;
}

void print_header(EFI_SYSTEM_TABLE *systemTable)
{
  Print(L"UEFI Version: %d.%02d\n",
        systemTable->Hdr.Revision >> 16,
        systemTable->Hdr.Revision & 0xFFFF);
}

void print_cpu_modes()
{
  UINT64 cr0, cr3, cr4, efer;

  asm volatile("mov %%cr0, %0" : "=r"(cr0));
  asm volatile("mov %%cr3, %0" : "=r"(cr3));
  asm volatile("mov %%cr4, %0" : "=r"(cr4));
  asm volatile("rdmsr" : "=A"(efer) : "c"(0xc0000080));

  Print(L"CR0 Register:  %016lx\n", cr0);
  Print(L"CR3 Register:  %016lx\n", cr3);
  Print(L"CR4 Register:  %016lx\n", cr4);
  Print(L"EFER Register: %016lx\n", efer);
  Print(L"Flags:        ");

  if (cr0 & (1 << 31))
  {
    Print(L" Paging");
  }

  if (cr4 & (1 << 5))
  {
    Print(L" PAE");
  }

  if (efer & (1 << 8))
  {
    Print(L" LME");
  }

  Print(L"\n");
}

void print_pae_table()
{
  UINT64 cr3, paeEntry;
  UINT64 *pdptTable, *pdptEntry;

  asm volatile("mov %%cr3, %0" : "=r"(cr3));
  pdptTable = (UINT64 *)cr3;

  for (int pdptIndex = 0; pdptIndex < 512; pdptIndex++)
  {
    if (pdptTable[pdptIndex] & 0x01)
    {
      pdptEntry = (UINT64 *)(pdptTable[pdptIndex] & ~0x0fff);
      for (int pdIndex = 0; pdIndex < 2; pdIndex++)
      {
        paeEntry = pdptEntry[pdIndex];
        if ((paeEntry & 0x0fff) && ((paeEntry & ~0x0fff) != 0))
        {
          Print(L"PAE Entry: PDPT %d, PD %d, Address: %08x, Flags: %08x\n",
                pdptIndex, pdIndex, paeEntry & ~0x0fff, paeEntry & 0x0fff);
        }
      }
    }
  }
}

EFI_STATUS get_memory_map(EFI_BOOT_SERVICES *bs, UINTN *mapKey)
{
  EFI_STATUS status;
  EFI_MEMORY_DESCRIPTOR *memoryMap = NULL;

  UINT32 descriptorVersion;
  UINTN memoryMapSize = 0;
  UINTN descriptorSize;

  status = uefi_call_wrapper(
      bs->GetMemoryMap, 5,
      &memoryMapSize, memoryMap, mapKey,
      &descriptorSize, &descriptorVersion);

  if (status != EFI_BUFFER_TOO_SMALL)
    return print_status(status, L"GetMemoryMap", 0);

  do
  {
    status = uefi_call_wrapper(
        bs->AllocatePool, 3,
        EfiLoaderData, memoryMapSize, &memoryMap);

    if (EFI_ERROR(status))
      return print_status(status, L"AllocatePool", 0);

    status = uefi_call_wrapper(
        bs->GetMemoryMap, 5,
        &memoryMapSize, memoryMap, mapKey,
        &descriptorSize, &descriptorVersion);

    if (status == EFI_BUFFER_TOO_SMALL)
      FreePool(memoryMap);

  } while (status == EFI_BUFFER_TOO_SMALL);

  if (EFI_ERROR(status))
      return print_status(status, L"GetMemoryMap", 0);

  return EFI_SUCCESS;
}

void print_gop_modes(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop)
{
  EFI_STATUS status;
  UINTN modeIndex, size;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;

  for (modeIndex = 0; modeIndex < gop->Mode->MaxMode; ++modeIndex)
  {
    status = uefi_call_wrapper(
        gop->QueryMode, 4,
        gop, modeIndex, &size, &info);

    if (EFI_ERROR(status))
      continue;

    Print(
        L"GOP Mode %d: %dx%d %s\n", modeIndex,
        info->HorizontalResolution,
        info->VerticalResolution,
        gop->Mode->Mode == modeIndex ? L" | Active " : L"");
  }
}

EFI_STATUS read_keystroke(EFI_SYSTEM_TABLE *systemTable, EFI_INPUT_KEY *key)
{
  EFI_STATUS status;
  UINTN eventIndex;

  status = uefi_call_wrapper(
      systemTable->BootServices->WaitForEvent, 3,
      1, &systemTable->ConIn->WaitForKey, &eventIndex);

  if (EFI_ERROR(status))
    return print_status(status, L"WaitForEvent", 0);

  status = uefi_call_wrapper(
      systemTable->ConIn->ReadKeyStroke, 2, systemTable->ConIn, &key);

  if (EFI_ERROR(status))
    return print_status(status, L"ReadKeyStroke", 0);

  return EFI_SUCCESS;
}

EFI_STATUS set_graphics(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINTN mode)
{
  EFI_STATUS status;

  status = uefi_call_wrapper(gop->SetMode, 2, gop, mode);
  if (EFI_ERROR(status))
    return status;

  Print(L"GOP Mode %d\n", mode);
  return EFI_SUCCESS;
}

EFI_STATUS
load_kernel(EFI_HANDLE imageHandle, EFI_BOOT_SERVICES *bs, void **kernel, UINT64 *kernelSize)
{
  EFI_STATUS status;
  EFI_LOADED_IMAGE_PROTOCOL *loaded;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
  EFI_FILE_PROTOCOL *root, *file;
  EFI_FILE_INFO *info;
  UINT64 size, bufferSize;
  void *buffer;

  status = uefi_call_wrapper(bs->HandleProtocol, 3, imageHandle, &gEfiLoadedImageProtocolGuid, &loaded);
  if (EFI_ERROR(status))
    return print_status(status, L"HandleProtocol", 0);

  status = uefi_call_wrapper(bs->HandleProtocol, 3, loaded->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, &fs);
  if (EFI_ERROR(status))
    return print_status(status, L"HandleProtocol", 0);

  status = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
  if (EFI_ERROR(status))
    return print_status(status, L"OpenVolume", 0);

  status = uefi_call_wrapper(root->Open, 5, root, &file, L"efi\\boot\\kernel.bin", EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(status))
    return print_status(status, L"Open", 0);

  info = LibFileInfo(file);
  if (!info)
    return EFI_BAD_BUFFER_SIZE;

  size = info->FileSize;

  FreePool(info);
  Print(L"Loaded %d bytes of kernel.\n", size);

  status = uefi_call_wrapper(bs->AllocatePool, 3, EfiLoaderCode, size, &buffer);
  if (EFI_ERROR(status))
    return status;

  bufferSize = size;
  status = uefi_call_wrapper(file->Read, 3, file, &bufferSize, buffer);
  if (EFI_ERROR(status) || bufferSize != size)
  {
    FreePool(buffer);
    return EFI_BAD_BUFFER_SIZE;
  }

  *kernel = buffer;
  *kernelSize = bufferSize;

  return status;
}

EFI_STATUS find_graphics(EFI_BOOT_SERVICES *bs, EFI_GRAPHICS_OUTPUT_PROTOCOL **gop)
{
  EFI_STATUS status;
  EFI_HANDLE *handleBuffer;
  UINTN handleCount, handleIndex;

  *gop = NULL;
  handleCount = 0;

  status = uefi_call_wrapper(
      bs->LocateHandleBuffer, 5,
      ByProtocol, &gEfiGraphicsOutputProtocolGuid,
      NULL, &handleCount, &handleBuffer);

  if (EFI_ERROR(status))
    return print_status(status, L"LocateHandleBuffer", 10);

  for (handleIndex = 0; handleIndex < handleCount; handleIndex++)
  {
    status = uefi_call_wrapper(
        bs->HandleProtocol, 3,
        handleBuffer[handleIndex],
        &gEfiGraphicsOutputProtocolGuid, gop);

    if (EFI_ERROR(status))
      continue;

    Print(
        L"Found Graphics at %d %dx%d at 0x%08x @ %d\n", handleIndex,
        (*gop)->Mode->Info->HorizontalResolution,
        (*gop)->Mode->Info->VerticalResolution,
        (*gop)->Mode->FrameBufferBase,
        (*gop)->Mode->FrameBufferSize);

    break;
  }

  FreePool(handleBuffer);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
efi_main(EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE *systemTable)
{
  EFI_STATUS status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
  EFI_INPUT_KEY key;
  UINTN mapKey;

  UINT64 kernelSize;
  void *kernel;
  void (*kernel_entry)(void *, UINTN);

  InitializeLib(imageHandle, systemTable);

  status = find_graphics(systemTable->BootServices, &gop);
  if (EFI_ERROR(status))
    return print_status(status, L"FindGraphics", 10);

  print_header(systemTable);
  print_gop_modes(gop);

  status = read_keystroke(systemTable, &key);
  if (EFI_ERROR(status))
    return print_status(status, L"ReadKeyStroke", 10);

  status = set_graphics(gop, key.UnicodeChar - '0');
  if (EFI_ERROR(status))
    return print_status(status, L"SetGraphics", 10);

  status = load_kernel(imageHandle, systemTable->BootServices, &kernel, &kernelSize);
  if (EFI_ERROR(status))
    return print_status(status, L"LoadKernel", 10);

  do
  {
    status = get_memory_map(systemTable->BootServices, &mapKey);
    if (EFI_ERROR(status))
      return print_status(status, L"GetMemoryMap", 10);

    status = uefi_call_wrapper(BS->ExitBootServices, 2, imageHandle, mapKey);
    if (EFI_ERROR(status))
      Print(L"%d %d\n", imageHandle, mapKey);

  } while (EFI_ERROR(status));

  kernel_entry = (void (*)(void *, UINTN))kernel;
  kernel_entry((void *)(gop->Mode->FrameBufferBase), gop->Mode->FrameBufferSize);

  return EFI_SUCCESS;
}
