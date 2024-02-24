#include <efi.h>
#include <efilib.h>

EFI_STATUS
print_status(EFI_STATUS status, CHAR16 *context, UINTN delay)
{
  Print(L"Something went wrong: %d, %s\n", status, context);

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

void print_text_modes(EFI_SIMPLE_TEXT_OUT_PROTOCOL *text)
{
  EFI_STATUS status;
  UINTN modeIndex, textColumns, textRows;

  for (modeIndex = 0; modeIndex < text->Mode->MaxMode; ++modeIndex)
  {
    status = uefi_call_wrapper(text->QueryMode, 4, text, modeIndex, &textColumns, &textRows);
    if (EFI_ERROR(status))
    {
      continue;
    }

    Print(L"Text Mode %d: %dx%d %s\n",
          modeIndex,
          textColumns,
          textRows,
          text->Mode->Mode == modeIndex ? L" | Active " : L"");
  }
}

EFI_STATUS
set_text_mode(EFI_SIMPLE_TEXT_OUT_PROTOCOL *text, UINTN textMode)
{
  EFI_STATUS status;

  status = uefi_call_wrapper(text->SetMode, 2, text, textMode);
  if (EFI_ERROR(status))
  {
    return status;
  }

  status = uefi_call_wrapper(text->ClearScreen, 2, text);
  if (EFI_ERROR(status))
  {
    return status;
  }

  status = uefi_call_wrapper(text->SetCursorPosition, 3, text, 0, 0);
  if (EFI_ERROR(status))
  {
    return status;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
efi_main(EFI_HANDLE imageHandle, EFI_SYSTEM_TABLE *systemTable)
{
  EFI_STATUS status;
  EFI_INPUT_KEY key;
  UINTN eventIndex;

  InitializeLib(imageHandle, systemTable);

  print_header(systemTable);
  print_text_modes(systemTable->ConOut);

  while (TRUE)
  {
    status = uefi_call_wrapper(BS->WaitForEvent, 3, 1, &systemTable->ConIn->WaitForKey, &eventIndex);
    if (EFI_ERROR(status))
    {
      return print_status(status, L"WaitForEvent", 10);
    }

    status = uefi_call_wrapper(systemTable->ConIn->ReadKeyStroke, 2, systemTable->ConIn, &key);
    if (EFI_ERROR(status))
    {
      return print_status(status, L"ReadKeyStroke", 10);
    }

    if (key.UnicodeChar >= '0' && key.UnicodeChar <= '9')
    {
      status = set_text_mode(systemTable->ConOut, key.UnicodeChar - '0');
      if (EFI_ERROR(status))
      {
        Print(L"Mode %d cannot be set: %d\n", key.UnicodeChar - '0', status);
        continue;
      }

      print_header(systemTable);
      print_text_modes(systemTable->ConOut);
    }

    if (key.UnicodeChar == 'x')
    {
      return EFI_SUCCESS;
    }
  }
}
