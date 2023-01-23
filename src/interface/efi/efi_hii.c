/*
 * Copyright (C) 2012 Michael Brown <mbrown@fensystems.co.uk>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 *
 * You can also choose to distribute this program under the terms of
 * the Unmodified Binary Distribution Licence (as given in the file
 * COPYING.UBDL), provided that you have satisfied its requirements.
 */

FILE_LICENCE ( GPL2_OR_LATER_OR_UBDL );

#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <ipxe/efi/efi.h>
#include <ipxe/efi/efi_strings.h>
#include <ipxe/efi/efi_hii.h>
#include <ipxe/efi/Protocol/HiiString.h>
#include <ipxe/efi/Protocol/HiiPopup.h>
#include <ipxe/efi/Protocol/HiiConfigRouting.h>
#include <ipxe/efi/Protocol/FormBrowser2.h>
#include <ipxe/efi/efi_ionic.h>

EFI_GUID gEfiHiiConfigRoutingProtocolGuid = EFI_HII_CONFIG_ROUTING_PROTOCOL_GUID;
EFI_HII_CONFIG_ROUTING_PROTOCOL   *gHiiConfigRouting = NULL;
EFI_FORM_BROWSER2_PROTOCOL        *mUefiFormBrowser2 = NULL;
CONST CHAR16 mConfigHdrTemplate[] = L"GUID=00000000000000000000000000000000&NAME=0000&PATH=00";
CHAR8    *gSupportedLanguages = NULL;

static void * efi_ifr_op ( struct efi_ifr_builder *ifr, unsigned int opcode,size_t len );

#define EFI_IFR_ONE_OF_OPTION_BASE_SIZE        6

///
/// Lookup table that converts EFI_IFR_TYPE_X enum values to a width in bytes
///
UINT8 mHiiDefaultTypeToWidth[] = {
    1, // EFI_IFR_TYPE_NUM_SIZE_8
    2, // EFI_IFR_TYPE_NUM_SIZE_16
    4, // EFI_IFR_TYPE_NUM_SIZE_32
    8, // EFI_IFR_TYPE_NUM_SIZE_64
    1, // EFI_IFR_TYPE_BOOLEAN
    3, // EFI_IFR_TYPE_TIME
    4, // EFI_IFR_TYPE_DATE
    2  // EFI_IFR_TYPE_STRING
};

UINT8 mNumericDefaultTypeToWidth[] = {
    3, // EFI_IFR_TYPE_NUM_SIZE_8
    6, // EFI_IFR_TYPE_NUM_SIZE_16
    12, // EFI_IFR_TYPE_NUM_SIZE_32
    24, // EFI_IFR_TYPE_NUM_SIZE_64
};

/** Tiano GUID */
static const EFI_GUID tiano_guid = EFI_IFR_TIANO_GUID;
EFI_GUID gEfiHiiStringProtocolGuid = EFI_HII_STRING_PROTOCOL_GUID;

EFI_HII_STRING_PROTOCOL        *gHiiString;

CHAR8 *
EFIAPI
GetBestLanguage (
    IN CONST CHAR8    *SupportedLanguages,
    IN UINTN        Iso639Language,
    ...
)
{
    VA_LIST        Args;
    CHAR8        *Language;
    UINTN        CompareLength;
    UINTN        LanguageLength;
    CONST CHAR8    *Supported;
    CHAR8        *BestLanguage;
    EFI_BOOT_SERVICES *bs = efi_systab->BootServices;

    VA_START (Args, Iso639Language);
    while ((Language = VA_ARG (Args, CHAR8 *)) != NULL) {
    //
    // Default to ISO 639-2 mode
    //
    CompareLength  = 3;
    LanguageLength = MIN (3, strlen (Language));

    //
    // If in RFC 4646 mode, then determine the length of the first RFC 4646 language code in Language
    //
    if (Iso639Language == 0) {
        for (LanguageLength = 0; Language[LanguageLength] != 0 && Language[LanguageLength] != ';'; LanguageLength++);
    }

    //
    // Trim back the length of Language used until it is empty
    //
    while (LanguageLength > 0) {
        //
        // Loop through all language codes in SupportedLanguages
        //
        for (Supported = SupportedLanguages; *Supported != '\0'; Supported += CompareLength) {
        //
        // In RFC 4646 mode, then Loop through all language codes in SupportedLanguages
        //
        if (Iso639Language == 0) {
          //
          // Skip ';' characters in Supported
          //
          for (; *Supported != '\0' && *Supported == ';'; Supported++);
          //
          // Determine the length of the next language code in Supported
          //
          for (CompareLength = 0; Supported[CompareLength] != 0 && Supported[CompareLength] != ';'; CompareLength++);
          //
          // If Language is longer than the Supported, then skip to the next language
          //
          if (LanguageLength > CompareLength) {
            continue;
          }
        }
        //
        // See if the first LanguageLength characters in Supported match Language
        //
        if (strncmp (Supported, Language, LanguageLength) == 0) {
          VA_END (Args);
          //
          // Allocate, copy, and return the best matching language code from SupportedLanguages
          //
          bs->AllocatePool ( EfiBootServicesData, CompareLength + 1 ,(VOID **)&BestLanguage );
            bs->SetMem(BestLanguage,CompareLength + 1,0);

          if (BestLanguage == NULL) {
            return NULL;
          }

          return generic_memcpy (BestLanguage, Supported, CompareLength);
        }
      }

      if (Iso639Language != 0) {
        //
        // If ISO 639 mode, then each language can only be tested once
        //
        LanguageLength = 0;
      } else {
        //
        // If RFC 4646 mode, then trim Language from the right to the next '-' character
        //
        for (LanguageLength--; LanguageLength > 0 && Language[LanguageLength] != '-'; LanguageLength--);
      }
    }
  }
  VA_END (Args);

  //
  // No matches were found
  //
  return NULL;
}

CHAR8 *
EFIAPI
HiiGetSupportedLanguages (
  IN EFI_HII_HANDLE           HiiHandle
  )
{
  EFI_STATUS  Status;
  UINTN       LanguageSize;
  CHAR8       TempSupportedLanguages;
  CHAR8       *SupportedLanguages;
  EFI_BOOT_SERVICES *bs = efi_systab->BootServices;


  //
  // Retrieve the size required for the supported languages buffer.
  //
  LanguageSize = 0;
  Status = gHiiString->GetLanguages (gHiiString, HiiHandle, &TempSupportedLanguages, &LanguageSize);

  //
  // If GetLanguages() returns EFI_SUCCESS for a zero size,
  // then there are no supported languages registered for HiiHandle.  If GetLanguages()
  // returns an error other than EFI_BUFFER_TOO_SMALL, then HiiHandle is not present
  // in the HII Database
  //
  if (Status != EFI_BUFFER_TOO_SMALL) {
    //
    // Return NULL if the size can not be retrieved, or if HiiHandle is not in the HII Database
    //
    return NULL;
  }

  //
  // Allocate the supported languages buffer.
  //
  bs->AllocatePool ( EfiBootServicesData, LanguageSize ,(VOID **)&SupportedLanguages );

  if (SupportedLanguages == NULL) {
    //
    // Return NULL if allocation fails.
    //
    return NULL;
  }
  bs->SetMem(SupportedLanguages,LanguageSize,0);

  //
  // Retrieve the supported languages string
  //
  Status = gHiiString->GetLanguages (gHiiString, HiiHandle, SupportedLanguages, &LanguageSize);
  if (EFI_ERROR (Status)) {
    //
    // Free the buffer and return NULL if the supported languages can not be retrieved.
    //
    bs->FreePool (SupportedLanguages);
    return NULL;
  }

  //
  // Return the Null-terminated ASCII string of supported languages
  //
  return SupportedLanguages;
}

EFI_STRING
EFIAPI
HiiGetString (
  IN EFI_HII_HANDLE  HiiHandle,
  IN EFI_STRING_ID   StringId,
  IN OUT UINT8        *StringLen,
  IN CONST CHAR8     *Language  OPTIONAL
  )
{
  EFI_STATUS  Status;
  UINTN       StringSize;
  CHAR16      TempString;
  EFI_STRING  String;
  CHAR8       *SupportedLanguages;
  CHAR8       *PlatformLanguage;
  CHAR8       *BestLanguage;
  EFI_BOOT_SERVICES *bs = efi_systab->BootServices;

  Status = bs->LocateProtocol ( &gEfiHiiStringProtocolGuid,
                                  NULL,
                                (void **) &gHiiString );
  //
  // Initialize all allocated buffers to NULL
  //
  SupportedLanguages = NULL;
  PlatformLanguage   = NULL;
  BestLanguage       = NULL;
  String             = NULL;

  //
  // Get the languages that the package specified by HiiHandle supports
  //
  SupportedLanguages = HiiGetSupportedLanguages (HiiHandle);
  if (SupportedLanguages == NULL) {
    goto Error;
  }
  //
  // If Languag is NULL, then set it to an empty string, so it will be
  // skipped by GetBestLanguage()
  //
  if (Language == NULL) {
    Language = "";
  }

  //
  // Get the best matching language from SupportedLanguages
  //
  BestLanguage = GetBestLanguage (
                   SupportedLanguages,
                   FALSE,                                             // RFC 4646 mode
                   Language,                                          // Highest priority
                   PlatformLanguage != NULL ? PlatformLanguage : "",  // Next highest priority
                   SupportedLanguages,                                // Lowest priority
                   NULL
                   );

  if (BestLanguage == NULL) {
    goto Error;
  }
  //
  // Retrieve the size of the string in the string package for the BestLanguage
  //
  StringSize = 0;
  Status = gHiiString->GetString (
                         gHiiString,
                         BestLanguage,
                         HiiHandle,
                         StringId,
                         &TempString,
                         &StringSize,
                         NULL
                         );
  //
  // If GetString() returns EFI_SUCCESS for a zero size,
  // then there are no supported languages registered for HiiHandle.  If GetString()
  // returns an error other than EFI_BUFFER_TOO_SMALL, then HiiHandle is not present
  // in the HII Database
  //
  if (Status != EFI_BUFFER_TOO_SMALL) {
    goto Error;
  }

  //
  // Allocate a buffer for the return string
  //
  bs->AllocatePool ( EfiBootServicesData, StringSize ,(VOID **)&String );

  if (String == NULL) {
    goto Error;
  }
  bs->SetMem(String,StringSize,0);
  //
  // Retrieve the string from the string package
  //
  Status = gHiiString->GetString (
                         gHiiString,
                         BestLanguage,
                         HiiHandle,
                         StringId,
                         String,
                         &StringSize,
                         NULL
                         );

  if (EFI_ERROR (Status)) {
    //
    // Free the buffer and return NULL if the supported languages can not be retrieved.
    //
    bs->FreePool (String);

    String = NULL;
  }

Error:
  //
  // Free allocated buffers
  //
  if (SupportedLanguages != NULL) {
    bs->FreePool (SupportedLanguages);

  }
  if (PlatformLanguage != NULL) {
    bs->FreePool (PlatformLanguage);
  }
  if (BestLanguage != NULL) {
    bs->FreePool (BestLanguage);
  }

  //
  // Return the Null-terminated Unicode string
  //
  *StringLen = StringSize;

  return String;
}


VOID
HiiSetString (
    IN  EFI_HII_HANDLE  HiiHandle,
    IN  CHAR16          *String,
    IN  EFI_STRING_ID   *StringId
)
{
  EFI_STATUS      Status;
  CHAR8*          Languages = NULL;
  UINTN           LangSize = 0;
  CHAR8*          CurrentLanguage;
  BOOLEAN         LastLanguage = FALSE;
  EFI_BOOT_SERVICES *gBS = efi_systab->BootServices;
  EFI_STRING_ID   StrId;

  StrId = *StringId;

  if (gHiiString == NULL) {

    Status = gBS->LocateProtocol ( &gEfiHiiStringProtocolGuid,
                                NULL,
                                (void **) &gHiiString );
    if (EFI_ERROR(Status)) {
      return;
    }
  }

  if (gSupportedLanguages == NULL) {
    Status = gHiiString->GetLanguages(gHiiString, HiiHandle, Languages, &LangSize);
        if (Status == EFI_BUFFER_TOO_SMALL) {
            Status = gBS->AllocatePool(EfiBootServicesData, LangSize, (VOID**)&Languages);
            if (EFI_ERROR(Status)) {
                //not enough resources to allocate string
                return;
            }
            Status = gHiiString->GetLanguages(gHiiString, HiiHandle, Languages, &LangSize);
            if(EFI_ERROR(Status)) {
                return;
            }
        }
        gSupportedLanguages=Languages;
    } else {
        Languages=gSupportedLanguages;
    }

    while(!LastLanguage) {

        CurrentLanguage = Languages;

        while(*Languages != ';' && *Languages != 0)
            Languages++;
        if (*Languages == 0) {
            LastLanguage = TRUE;
            Status = gHiiString->SetString(gHiiString, HiiHandle, StrId, CurrentLanguage, String, NULL);

            if (EFI_ERROR(Status)) {
                return;
            }
        } else {
            *Languages = 0;
            Status = gHiiString->SetString(gHiiString, HiiHandle, StrId, CurrentLanguage, String, NULL);
            *Languages = ';';
            Languages++;
            if (EFI_ERROR(Status)) {
                return;
            }
        }
    }
}

unsigned int
UnicodeSPrint (
  OUT CHAR16        *Buffer,
  IN  UINTN         BufferSize,
  const char         *fmt,
  ...
  )
{
  va_list args;

  int   NumberOfPrinted;

  va_start ( args, fmt );
  NumberOfPrinted = efi_vsnprintf ( Buffer, BufferSize, fmt, args );
  va_end ( args );

  return NumberOfPrinted;
}

VOID *
EFIAPI InternalMemSetMem16 (
  OUT     VOID                      *Buffer,
  IN      UINTN                     Length,
  IN      UINT16                    Value
  )
{
  for (; Length != 0; Length--) {
    ((UINT16*)Buffer)[Length - 1] = Value;
  }
  return Buffer;
}

UINTN EFIAPI InternalStrLen (
  IN      CONST CHAR16              *String
  )
{
  UINTN                             Length;

  if(((UINTN) String & BIT0) != 0) return 0;

  for (Length = 0; *String != L'\0'; String++, Length++) {
  }
  return Length;
}

CHAR16 * InternalStrCpy ( CHAR16 *dest, const CHAR16 *src )
{
	const UINT16 *src_bytes = ( ( const UINT16 * ) src );
	UINT16 *dest_bytes = ( ( UINT16 * ) dest );

	/* We cannot use strncpy(), since that would pad the destination */
	for ( ; ; src_bytes++, dest_bytes++ ) {
		*dest_bytes = *src_bytes;
		if ( ! *dest_bytes )
			break;
	}
	return dest;
}

CHAR16 * InternalStrCat ( CHAR16 *dest, const CHAR16 *src )
{
	InternalStrCpy ( ( dest + InternalStrLen ( dest ) ), src );
	return dest;
}

CHAR16 * InternalStrnCpy ( CHAR16 *dest, const CHAR16 *src, UINT8 max )
{
	const UINT16 *src_bytes = ( ( const UINT16 * ) src );
	UINT16 *dest_bytes = ( ( UINT16 * ) dest );

	for ( ; max ; max--, src_bytes++, dest_bytes++ ) {
		*dest_bytes = *src_bytes;
		if ( ! *dest_bytes )
			break;
	}
	while ( max-- )
		*(dest_bytes++) = '\0';
	return dest;
}

VOID
EFIAPI
CreatePopUp (
  IN  UINTN          Attribute,
  OUT EFI_INPUT_KEY  *Key,      OPTIONAL
  ...
  )
{
  EFI_STATUS                       Status;
  VA_LIST                          Args;
  EFI_BOOT_SERVICES                *gBS = efi_systab->BootServices;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *ConOut = efi_systab->ConOut;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL   *ConIn = efi_systab->ConIn;
  EFI_SIMPLE_TEXT_OUTPUT_MODE      SavedConsoleMode;
  UINTN                            Columns;
  UINTN                            Rows;
  UINTN                            Column;
  UINTN                            Row;
  UINTN                            NumberOfLines;
  UINTN                            MaxLength;
  CHAR16                           *String;
  UINTN                            Length;
  CHAR16                           *Line = NULL;
  UINTN                            EventIndex;

  VA_START (Args, Key);
  MaxLength = 0;
  NumberOfLines = 0;
  while ((String = VA_ARG (Args, CHAR16 *)) != NULL) {
    MaxLength = MAX (MaxLength, InternalStrLen (String));
    NumberOfLines++;
  }
  VA_END (Args);

  gBS->CopyMem(&SavedConsoleMode,ConOut->Mode,sizeof (SavedConsoleMode));

  ConOut->QueryMode (ConOut, SavedConsoleMode.Mode, &Columns, &Rows);

  ConOut->EnableCursor (ConOut, FALSE);
  ConOut->SetAttribute (ConOut, Attribute);

  NumberOfLines = MIN (NumberOfLines, Rows - 3);

  MaxLength = MIN (MaxLength, Columns - 2);

  Row    = (Rows - (NumberOfLines + 3)) / 2;
  Column = (Columns - (MaxLength + 2)) / 2;

  gBS->AllocatePool ( EfiBootServicesData, (MaxLength + 3) * sizeof (CHAR16) ,(VOID **)&Line );

  gBS->SetMem((UINT8 *)Line,(MaxLength + 3) * sizeof (CHAR16) ,0xAA);

  InternalMemSetMem16 (Line, (MaxLength + 2), BOXDRAW_HORIZONTAL);

  Line[0] = BOXDRAW_DOWN_RIGHT;
  Line[MaxLength + 1] = BOXDRAW_DOWN_LEFT;
  Line[MaxLength + 2] = L'\0';

  ConOut->SetCursorPosition (ConOut, Column, Row++);
  ConOut->OutputString (ConOut, Line);

  // Draw middle of the popup with strings
  //
  VA_START (Args, Key);
  while ((String = VA_ARG (Args, CHAR16 *)) != NULL && NumberOfLines > 0) {
    Length = InternalStrLen (String);
    InternalMemSetMem16 (Line, MaxLength + 2, L' ');
    if (Length <= MaxLength) {
      //
      // Length <= MaxLength
      //
      gBS->CopyMem(Line + 1 + (MaxLength - Length) / 2, String, Length * sizeof (CHAR16));
    } else {
      //
      // Length > MaxLength
      //
      gBS->CopyMem(Line + 1, String + (Length - MaxLength) / 2 ,MaxLength * sizeof (CHAR16));
    }
    Line[0]             = BOXDRAW_VERTICAL;
    Line[MaxLength + 1] = BOXDRAW_VERTICAL;
    Line[MaxLength + 2] = L'\0';
    ConOut->SetCursorPosition (ConOut, Column, Row++);
    ConOut->OutputString (ConOut, Line);
    NumberOfLines--;
  }
  VA_END (Args);

  //
  // Draw bottom of popup box
  //
  InternalMemSetMem16 (Line, (MaxLength + 2), BOXDRAW_HORIZONTAL);
  Line[0]             = BOXDRAW_UP_RIGHT;
  Line[MaxLength + 1] = BOXDRAW_UP_LEFT;
  Line[MaxLength + 2] = L'\0';
  ConOut->SetCursorPosition (ConOut, Column, Row++);
  ConOut->OutputString (ConOut, Line);

  //
  // Free the allocated line buffer
  //
  gBS->FreePool (Line);
  ConOut->EnableCursor      (ConOut, SavedConsoleMode.CursorVisible);
  ConOut->SetCursorPosition (ConOut, SavedConsoleMode.CursorColumn, SavedConsoleMode.CursorRow);
  ConOut->SetAttribute      (ConOut, SavedConsoleMode.Attribute);

  // Wait for a keystroke
  //
  if (Key != NULL) {
    while (TRUE) {
      Status = ConIn->ReadKeyStroke (ConIn, Key);
      if (!EFI_ERROR (Status)) {
        break;
      }
      if (Status != EFI_NOT_READY) {
        continue;
      }
      gBS->WaitForEvent (1, &ConIn->WaitForKey, &EventIndex);
    }
  }
}

BOOLEAN
EFIAPI
HiiCreatePopUp (
  IN  UINT8             PopupStyle,
  IN  UINT8        		  PopupType,
  IN  EFI_HII_HANDLE    HiiHandle,
  IN  EFI_STRING_ID     Message
)
{
  EFI_BOOT_SERVICES         *gBS = efi_systab->BootServices;
  EFI_STATUS                Status;
  EFI_HII_POPUP_PROTOCOL    *HiiPopupProtocol = NULL;
  EFI_GUID                  EfiHiiPopupProtocolGuid = EFI_HII_POPUP_PROTOCOL_GUID;
  EFI_INPUT_KEY             Key;
  EFI_STRING                String;
  UINT8                     StringLen;

  Status = gBS->LocateProtocol (&EfiHiiPopupProtocolGuid, NULL, (VOID **) &HiiPopupProtocol);
  if (EFI_ERROR (Status) || HiiPopupProtocol == NULL) {

    String = HiiGetString(HiiHandle,Message,&StringLen,"English");

    CreatePopUp (
                EFI_LIGHTGRAY | EFI_BACKGROUND_BLUE,
                &Key,
                String,
                NULL
              );
    return TRUE;
  }

  Status = HiiPopupProtocol->CreatePopup(HiiPopupProtocol, \
                                          PopupStyle, \
                                          PopupType, \
                                          HiiHandle, \
                                          Message, \
                                          NULL);

  if (EFI_ERROR (Status)) {
      return FALSE;
  }

  return TRUE;
}

void efi_ifr_checkbox_op ( struct efi_ifr_builder *ifr,
             unsigned int prompt_id,unsigned int help_id,
             unsigned int question_id,
             unsigned int varstore_id,
             unsigned int varstore_info __unused,
             unsigned int varstore_offset,
             unsigned int vflags,
             unsigned int flags ) {

    EFI_IFR_CHECKBOX *OpCode;

    /* Add opcode */
    OpCode = efi_ifr_op ( ifr, EFI_IFR_CHECKBOX_OP, sizeof ( *OpCode ) );
    if ( ! OpCode )
        return;

    OpCode->Question.QuestionId             = question_id;
      OpCode->Question.VarStoreId             = varstore_id;
      OpCode->Question.VarStoreInfo.VarOffset = varstore_offset;
      OpCode->Question.Header.Prompt          = prompt_id;
      OpCode->Question.Header.Help            = help_id;
      OpCode->Question.Flags                  = vflags;
      OpCode->Flags                           = flags;
}

void
efi_ifr_oneofoption_op ( struct efi_ifr_builder *ifr,
  IN UINT16  StringId,
  IN UINT8   Flags,
  IN UINT8   Type,
  IN UINT64  Value
  )
{
    EFI_IFR_ONE_OF_OPTION *OpCode;
    EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
    UINTN	ValueLen = mHiiDefaultTypeToWidth[Type];

    /* Add opcode */
    OpCode = efi_ifr_op ( ifr, EFI_IFR_ONE_OF_OPTION_OP, EFI_IFR_ONE_OF_OPTION_BASE_SIZE + ValueLen);
    if ( ! OpCode )
        return;

    OpCode->Option = StringId;
    OpCode->Flags = (UINT8) (Flags & (EFI_IFR_OPTION_DEFAULT | EFI_IFR_OPTION_DEFAULT_MFG));
    OpCode->Type = Type;
    bs->CopyMem(&OpCode->Value,&Value,ValueLen);
}

void
efi_ifr_OneOf_Op (  struct efi_ifr_builder *ifr,
  EFI_QUESTION_ID  QuestionId,
  EFI_VARSTORE_ID  VarStoreId,
  UINT16           VarInfo,
  EFI_STRING_ID    Prompt,
  EFI_STRING_ID    Help,
  UINT8            QuestionFlags,
  UINT8            OneOfFlags
  )
{
    EFI_IFR_ONE_OF  *OpCode;

    /* Add opcode */
    OpCode = efi_ifr_op ( ifr, EFI_IFR_ONE_OF_OP, 17);
    if ( ! OpCode )
        return;

    OpCode->Header.Scope = 1;
      OpCode->Question.Header.Prompt          = Prompt;
      OpCode->Question.Header.Help            = Help;
      OpCode->Question.QuestionId             = QuestionId;
      OpCode->Question.VarStoreId             = VarStoreId;
      OpCode->Question.VarStoreInfo.VarOffset = VarInfo;
      OpCode->Question.Flags                  = QuestionFlags;
      OpCode->Flags                           = OneOfFlags;

}

EFI_STRING
EFIAPI
InternalHiiBrowserCallback (
  IN CONST EFI_GUID    *VariableGuid,
  IN CONST CHAR16      *VariableName,
  IN CONST EFI_STRING  SetResultsData
  )
{
  EFI_STATUS  Status;
  UINTN       ResultsDataSize = 0;
  EFI_STRING  ResultsData;
  CHAR16      TempResultsData;
  EFI_BOOT_SERVICES *gBS = efi_systab->BootServices;
  EFI_GUID  gEfiFormBrowser2ProtocolGuid = EFI_FORM_BROWSER2_PROTOCOL_GUID;

  if (mUefiFormBrowser2 == NULL) {
    Status = gBS->LocateProtocol (&gEfiFormBrowser2ProtocolGuid, NULL, (VOID **) &mUefiFormBrowser2);
    if (EFI_ERROR (Status) || mUefiFormBrowser2 == NULL) {
      return NULL;
    }
  }

  ResultsDataSize = 0;

  if (SetResultsData != NULL) {
    //
    // Request to to set data in the uncommitted browser state information
    //
    ResultsData = SetResultsData;
  } else {
    //
    // Retrieve the length of the buffer required ResultsData from the Browser Callback
    //
    Status = mUefiFormBrowser2->BrowserCallback (
                              mUefiFormBrowser2,
                              &ResultsDataSize,
                              &TempResultsData,
                              TRUE,
                              VariableGuid,
                              VariableName
                              );

    if (!EFI_ERROR (Status)) {
      //
      // No Resluts Data, only allocate one char for '\0'
      //

      gBS->AllocatePool ( EfiBootServicesData, sizeof (CHAR16) ,(VOID **)&ResultsData );
      gBS->SetMem(ResultsData,sizeof (CHAR16),0);

      return ResultsData;
    }

    if (Status != EFI_BUFFER_TOO_SMALL) {
      return NULL;
    }

    gBS->AllocatePool ( EfiBootServicesData, ResultsDataSize ,(VOID **)&ResultsData );
    gBS->SetMem(ResultsData,ResultsDataSize,0);

    if (ResultsData == NULL) {
      return NULL;
    }
  }

  Status = mUefiFormBrowser2->BrowserCallback (
                            mUefiFormBrowser2,
                            &ResultsDataSize,
                            ResultsData,
                            (BOOLEAN)(SetResultsData == NULL),
                            VariableGuid,
                            VariableName
                            );
  if (EFI_ERROR (Status)) {
    return NULL;
  }

  return ResultsData;
}

EFI_STRING
EFIAPI
InternalHiiBlockToConfig (
  IN CONST EFI_STRING  ConfigRequest,
  IN CONST UINT8       *Block,
  IN UINTN             BlockSize
  )
{
  EFI_STATUS  Status;
  EFI_STRING  ConfigResp;
  CHAR16      *Progress;
  EFI_BOOT_SERVICES *gBS = efi_systab->BootServices;

  if(gHiiConfigRouting == NULL){
    Status = gBS->LocateProtocol (&gEfiHiiConfigRoutingProtocolGuid, NULL, (VOID **) &gHiiConfigRouting);
  }

  Status = gHiiConfigRouting->BlockToConfig (
                                gHiiConfigRouting,
                                ConfigRequest,
                                Block,
                                BlockSize,
                                &ConfigResp,
                                &Progress
                                );

  if (EFI_ERROR (Status)) {
    return NULL;
  }
  return ConfigResp;
}

BOOLEAN
EFIAPI
HiiGetBrowserData (
  IN CONST EFI_GUID  *VariableGuid,
  IN CONST CHAR16    *VariableName,
  IN UINTN           BufferSize,
  OUT UINT8          *Buffer
  )
{
  EFI_STRING  ResultsData;
  UINTN       Size;
  EFI_STRING  ConfigResp;
  EFI_STATUS  Status;
  CHAR16      *Progress;
  EFI_BOOT_SERVICES *gBS = efi_systab->BootServices;

  if(gHiiConfigRouting == NULL){
    Status = gBS->LocateProtocol (&gEfiHiiConfigRoutingProtocolGuid, NULL, (VOID **) &gHiiConfigRouting);
  }
  //
  // Retrieve the results data from the Browser Callback
  //
  ResultsData = InternalHiiBrowserCallback (VariableGuid, VariableName, NULL);

  if (ResultsData == NULL) {
    return FALSE;
  }

  Size = (InternalStrLen (mConfigHdrTemplate) + 1) * sizeof (CHAR16);
  Size = Size + (InternalStrLen (ResultsData) + 1) * sizeof (CHAR16);

  gBS->AllocatePool ( EfiBootServicesData, Size ,(VOID **)&ConfigResp );
  gBS->SetMem(ConfigResp,Size,0);

  efi_snprintf ( ConfigResp, Size,"%ls&%ls", mConfigHdrTemplate, ResultsData);

  gBS->FreePool (ResultsData);

  if (ConfigResp == NULL) {
    return FALSE;
  }

  Status = gHiiConfigRouting->ConfigToBlock (
                                gHiiConfigRouting,
                                ConfigResp,
                                Buffer,
                                &BufferSize,
                                &Progress
                                );

  gBS->FreePool (ConfigResp);

  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  return TRUE;
}

BOOLEAN
EFIAPI
HiiSetBrowserData (
  IN CONST EFI_GUID  *VariableGuid,
  IN CONST CHAR16    *VariableName,
  IN UINTN           BufferSize,
  IN CONST UINT8     *Buffer
  )
{
  UINTN       Size;
  EFI_STRING  ConfigRequest;
  EFI_STRING  ConfigResp;
  EFI_STRING  ResultsData;
  EFI_BOOT_SERVICES *gBS = efi_systab->BootServices;

  Size = (InternalStrLen (mConfigHdrTemplate) + 32 + 1) * sizeof (CHAR16);

  gBS->AllocatePool ( EfiBootServicesData, Size ,(VOID **)&ConfigRequest );
  gBS->SetMem(ConfigRequest,Size,0);

  efi_snprintf ( ConfigRequest, Size,"%ls&OFFSET=0&WIDTH=%x", mConfigHdrTemplate, BufferSize);

  if (ConfigRequest == NULL) {
    return FALSE;
  }

  ConfigResp = InternalHiiBlockToConfig (ConfigRequest, Buffer, BufferSize);

  gBS->FreePool (ConfigRequest);
  if (ConfigResp == NULL) {
    return FALSE;
  }

  ResultsData = InternalHiiBrowserCallback (VariableGuid, VariableName, ConfigResp + InternalStrLen(mConfigHdrTemplate) + 1);

  gBS->FreePool (ConfigResp);

  return (BOOLEAN)(ResultsData != NULL);
}

void efi_ifr_suppress_grayout_op ( struct efi_ifr_builder *ifr,
                              UINT16 		QuestionId,
                              UINT16 		Value,
                              BOOLEAN     Suppress    //if TRUE Suppress; False Gray out.
) {

    EFI_IFR_OP_HEADER   *OpCode;
    EFI_IFR_EQ_ID_VAL   *Condition;

    /* Add opcode */
    if(Suppress) {
        OpCode = efi_ifr_op ( ifr, EFI_IFR_SUPPRESS_IF_OP, sizeof ( *OpCode ) );
    } else {
        OpCode = efi_ifr_op ( ifr, EFI_IFR_GRAY_OUT_IF_OP, sizeof ( *OpCode ) );
    }
    if ( ! OpCode )
        return;

    OpCode->Scope = 1;

    Condition = efi_ifr_op ( ifr, EFI_IFR_EQ_ID_VAL_OP, sizeof ( *Condition ) );

    if ( ! Condition )
        return;

    Condition->Header.Scope = 0;

    //Then goes Opcode Data..
    Condition->QuestionId = QuestionId;
    Condition->Value = Value;
}


/**
 * Add EFI_IFR_REF_OP
 *
 * @v ifr           IFR builder
 * @v form_id       Title string identifier
 * @v Promt         String ID for Promt
 * @v Help          String ID for Help
 * @v QuestionFlags Flags in Question Header
 * @v QuestionID    Question ID
 * @ret A pointer tp tje created opcode.
 */
void efi_ifr_goto_op ( struct efi_ifr_builder *ifr,
                   unsigned int form_id,
                   unsigned int Promt,
                   unsigned int Help,
                   unsigned int QuestionFlags,
                   unsigned int QuestionId ) {


    EFI_IFR_REF *OpCode;

    /* Add opcode */
    OpCode = efi_ifr_op ( ifr, EFI_IFR_REF_OP, sizeof ( *OpCode ) );
    if ( ! OpCode )
        return;

    OpCode->Question.Header.Prompt = Promt;
    OpCode->Question.Header.Help = Help;
    OpCode->Question.QuestionId = QuestionId;
    OpCode->Question.Flags = QuestionFlags;
    OpCode->Question.VarStoreId = 0;
    OpCode->Question.VarStoreInfo.VarOffset = 0xFFFF;
    OpCode->FormId = form_id;
}

/**
 * Add string to IFR builder
 *
 * @v ifr		IFR builder
 * @v fmt		Format string
 * @v ...		Arguments
 * @ret string_id	String identifier, or zero on failure
 */
unsigned int efi_ifr_string ( struct efi_ifr_builder *ifr, const char *fmt,
			      ... ) {
	EFI_HII_STRING_BLOCK *new_strings;
	EFI_HII_SIBT_STRING_UCS2_BLOCK *ucs2;
	size_t new_strings_len;
	va_list args;
	size_t len;
	unsigned int string_id;

	/* Do nothing if a previous allocation has failed */
	if ( ifr->failed )
		return 0;

	/* Calculate string length */
	va_start ( args, fmt );
	len = ( efi_vsnprintf ( NULL, 0, fmt, args ) + 1 /* wNUL */ );
	va_end ( args );

	/* Reallocate strings */
	new_strings_len = ( ifr->strings_len +
			    offsetof ( typeof ( *ucs2 ), StringText ) +
			    ( len * sizeof ( ucs2->StringText[0] ) ) );
	new_strings = realloc ( ifr->strings, new_strings_len );
	if ( ! new_strings ) {
		ifr->failed = 1;
		return 0;
	}
	ucs2 = ( ( ( void * ) new_strings ) + ifr->strings_len );
	ifr->strings = new_strings;
	ifr->strings_len = new_strings_len;

	/* Fill in string */
	ucs2->Header.BlockType = EFI_HII_SIBT_STRING_UCS2;
	va_start ( args, fmt );
	efi_vsnprintf ( ucs2->StringText, len, fmt, args );
	va_end ( args );

	/* Allocate string ID */
	string_id = ++(ifr->string_id);

	DBGC ( ifr, "IFR %p string %#04x is \"%ls\"\n",
	       ifr, string_id, ucs2->StringText );
	return string_id;
}

/**
 * Add string to IFR builder
 *
 * @v ifr		IFR builder
 * @v fmt		Format string
 * @v ...		Arguments
 * @ret string_id	String identifier, or zero on failure
 */
unsigned int efi_ifr_string_xuefi ( struct efi_ifr_builder *ifr,EFI_STRING_ID prompt_id, const char *fmt,
                  ... ) {
    EFI_HII_STRING_BLOCK *new_strings;
    EFI_HII_XUEFI_BLOCK *xuefi;
    EFI_HII_SIBT_SKIP2_BLOCK  *skip2;
    EFI_HII_SIBT_STRING_UCS2_BLOCK *ucs2;
    size_t new_strings_len;
    va_list args;
    size_t len;
    unsigned int string_id;

    if ( ifr->failed )
        return 0;

    va_start ( args, fmt );
    len = ( efi_vsnprintf ( NULL, 0, fmt, args ) + 1 );
    va_end ( args );

    new_strings_len = ( ifr->uefi_strings_len + offsetof ( typeof ( *ucs2 ), StringText ) + ( len * sizeof ( ucs2->StringText[0] ) ) + sizeof( *skip2) );

    new_strings = realloc ( ifr->uefi_strings, new_strings_len );

    if ( ! new_strings ) {
    ifr->failed = 1;
    return 0;
    }

    // fill skip2
    xuefi = ( ( ( void * ) new_strings ) + ifr->uefi_strings_len );
    xuefi->skip2.Header.BlockType = EFI_HII_SIBT_SKIP2;
    xuefi->skip2.SkipCount =  ifr->string_id - ifr->uefi_string_index - 1;

    // fill ucs2
    xuefi->ucs2.Header.BlockType = EFI_HII_SIBT_STRING_UCS2;

    va_start ( args, fmt );
    efi_vsnprintf ( xuefi->ucs2.StringText, len, fmt, args );
    va_end ( args );

    ifr->uefi_strings = new_strings;
    ifr->uefi_strings_len = new_strings_len;
    ifr->uefi_string_index = prompt_id;

    return string_id;
}

/**
 * Add IFR opcode to IFR builder
 *
 * @v ifr		IFR builder
 * @v opcode		Opcode
 * @v len		Opcode length
 * @ret op		Opcode, or NULL
 */
static void * efi_ifr_op ( struct efi_ifr_builder *ifr, unsigned int opcode,
			   size_t len ) {

	EFI_IFR_OP_HEADER *new_ops;
	EFI_IFR_OP_HEADER *op;
	size_t new_ops_len;

	/* Do nothing if a previous allocation has failed */
	if ( ifr->failed )
		return NULL;

	/* Reallocate opcodes */
	new_ops_len = ( ifr->ops_len + len );
	new_ops = realloc ( ifr->ops, new_ops_len );
	if ( ! new_ops ) {
		ifr->failed = 1;
		return NULL;
	}
	op = ( ( ( void * ) new_ops ) + ifr->ops_len );
	ifr->ops = new_ops;
	ifr->ops_len = new_ops_len;

	/* Fill in opcode header */
	memset ( op, 0, len );
	op->OpCode = opcode;
	op->Length = len;

	return op;
}

/**
 * Add end opcode to IFR builder
 *
 * @v ifr		IFR builder
 */
void efi_ifr_end_op ( struct efi_ifr_builder *ifr ) {
	size_t dispaddr = ifr->ops_len;
	EFI_IFR_END *end;

	/* Add opcode */
	end = efi_ifr_op ( ifr, EFI_IFR_END_OP, sizeof ( *end ) );

	DBGC ( ifr, "IFR %p end\n", ifr );
	DBGC2_HDA ( ifr, dispaddr, end, sizeof ( *end ) );
}

/**
 * Add false opcode to IFR builder
 *
 * @v ifr		IFR builder
 */
void efi_ifr_false_op ( struct efi_ifr_builder *ifr ) {
	size_t dispaddr = ifr->ops_len;
	EFI_IFR_FALSE *false;

	/* Add opcode */
	false = efi_ifr_op ( ifr, EFI_IFR_FALSE_OP, sizeof ( *false ) );

	DBGC ( ifr, "IFR %p false\n", ifr );
	DBGC2_HDA ( ifr, dispaddr, false, sizeof ( *false ) );
}

/**
 * Add form opcode to IFR builder
 *
 * @v ifr		IFR builder
 * @v title_id		Title string identifier
 * @ret form_id		Form identifier
 */
unsigned int efi_ifr_form_op ( struct efi_ifr_builder *ifr,
			       unsigned int title_id ) {
	size_t dispaddr = ifr->ops_len;
	EFI_IFR_FORM *form;

	/* Add opcode */
	form = efi_ifr_op ( ifr, EFI_IFR_FORM_OP, sizeof ( *form ) );
	if ( ! form )
		return 0;
	form->Header.Scope = 1;
	form->FormId = ++(ifr->form_id);
	form->FormTitle = title_id;

	DBGC ( ifr, "IFR %p name/value store %#04x title %#04x\n",
	       ifr, form->FormId, title_id );
	DBGC2_HDA ( ifr, dispaddr, form, sizeof ( *form ) );
	return form->FormId;
}

unsigned int efi_ifr_form_op_ex ( struct efi_ifr_builder *ifr,
                   unsigned int form_id,
                   unsigned int title_id ) {
    EFI_IFR_FORM *form;

    /* Add opcode */
    form = efi_ifr_op ( ifr, EFI_IFR_FORM_OP, sizeof ( *form ) );
    if ( ! form )
        return 0;
    form->Header.Scope = 1;
    form->FormId = form_id;
    form->FormTitle = title_id;

    return form->FormId;
}

/**
 * Add formset opcode to IFR builder
 *
 * @v ifr		IFR builder
 * @v guid		GUID
 * @v title_id		Title string identifier
 * @v help_id		Help string identifier
 * @v ...		Class GUIDs (terminated by NULL)
 */
void efi_ifr_form_set_op ( struct efi_ifr_builder *ifr, const EFI_GUID *guid,
			   unsigned int title_id, unsigned int help_id, ... ) {
	size_t dispaddr = ifr->ops_len;
	EFI_IFR_FORM_SET *formset;
	EFI_GUID *class_guid;
	unsigned int num_class_guids = 0;
	size_t len;
	va_list args;

	/* Count number of class GUIDs */
	va_start ( args, help_id );
	while ( va_arg ( args, const EFI_GUID * ) != NULL )
		num_class_guids++;
	va_end ( args );

	/* Add opcode */
	len = ( sizeof ( *formset ) +
		( num_class_guids * sizeof ( *class_guid ) ) );
	formset = efi_ifr_op ( ifr, EFI_IFR_FORM_SET_OP, len );
	if ( ! formset )
		return;
	formset->Header.Scope = 1;
	memcpy ( &formset->Guid, guid, sizeof ( formset->Guid ) );
	formset->FormSetTitle = title_id;
	formset->Help = help_id;
	formset->Flags = num_class_guids;

	/* Add class GUIDs */
	class_guid = ( ( ( void * ) formset ) + sizeof ( *formset ) );
	va_start ( args, help_id );
	while ( num_class_guids-- ) {
		memcpy ( class_guid++, va_arg ( args, const EFI_GUID * ),
			 sizeof ( *class_guid ) );
	}
	va_end ( args );

	DBGC ( ifr, "IFR %p formset title %#04x help %#04x\n",
	       ifr, title_id, help_id );
	DBGC2_HDA ( ifr, dispaddr, formset, len );
}

/**
 * Add get opcode to IFR builder
 *
 * @v ifr		IFR builder
 * @v varstore_id	Variable store identifier
 * @v varstore_info	Variable string identifier or offset
 * @v varstore_type	Variable type
 */
void efi_ifr_get_op ( struct efi_ifr_builder *ifr, unsigned int varstore_id,
		      unsigned int varstore_info, unsigned int varstore_type ) {
	size_t dispaddr = ifr->ops_len;
	EFI_IFR_GET *get;

	/* Add opcode */
	get = efi_ifr_op ( ifr, EFI_IFR_GET_OP, sizeof ( *get ) );
	get->VarStoreId = varstore_id;
	get->VarStoreInfo.VarName = varstore_info;
	get->VarStoreType = varstore_type;

	DBGC ( ifr, "IFR %p get varstore %#04x:%#04x type %#02x\n",
	       ifr, varstore_id, varstore_info, varstore_type );
	DBGC2_HDA ( ifr, dispaddr, get, sizeof ( *get ) );
}

/**
 * Add GUID class opcode to IFR builder
 *
 * @v ifr		IFR builder
 * @v class		Class
 */
void efi_ifr_guid_class_op ( struct efi_ifr_builder *ifr, unsigned int class ) {
	size_t dispaddr = ifr->ops_len;
	EFI_IFR_GUID_CLASS *guid_class;

	/* Add opcode */
	guid_class = efi_ifr_op ( ifr, EFI_IFR_GUID_OP,
				  sizeof ( *guid_class ) );
	if ( ! guid_class )
		return;
	memcpy ( &guid_class->Guid, &tiano_guid, sizeof ( guid_class->Guid ) );
	guid_class->ExtendOpCode = EFI_IFR_EXTEND_OP_CLASS;
	guid_class->Class = class;

	DBGC ( ifr, "IFR %p GUID class %#02x\n", ifr, class );
	DBGC2_HDA ( ifr, dispaddr, guid_class, sizeof ( *guid_class ) );
}

/**
 * Add GUID subclass opcode to IFR builder
 *
 * @v ifr		IFR builder
 * @v subclass		Subclass
 */
void efi_ifr_guid_subclass_op ( struct efi_ifr_builder *ifr,
				unsigned int subclass ) {
	size_t dispaddr = ifr->ops_len;
	EFI_IFR_GUID_SUBCLASS *guid_subclass;

	/* Add opcode */
	guid_subclass = efi_ifr_op ( ifr, EFI_IFR_GUID_OP,
				     sizeof ( *guid_subclass ) );
	if ( ! guid_subclass )
		return;
	memcpy ( &guid_subclass->Guid, &tiano_guid,
		 sizeof ( guid_subclass->Guid ) );
	guid_subclass->ExtendOpCode = EFI_IFR_EXTEND_OP_SUBCLASS;
	guid_subclass->SubClass = subclass;

	DBGC ( ifr, "IFR %p GUID subclass %#02x\n", ifr, subclass );
	DBGC2_HDA ( ifr, dispaddr, guid_subclass, sizeof ( *guid_subclass ) );
}

/**
 * Add numeric opcode to IFR builder
 *
 * @v ifr		IFR builder
 * @v prompt_id		Prompt string identifier
 * @v help_id		Help string identifier
 * @v question_id	Question identifier
 * @v varstore_id	Variable store identifier
 * @v varstore_info	Variable string identifier or offset
 * @v vflags		Variable flags
 * @v min_value		Minimum value
 * @v max_value		Maximum value
 * @v step		Step
 * @v flags		Flags
 */
void efi_ifr_numeric_op ( struct efi_ifr_builder *ifr, unsigned int prompt_id,
			  unsigned int help_id, unsigned int question_id,
			  unsigned int varstore_id, unsigned int varstore_info,
			  unsigned int vflags, unsigned long min_value,
			  unsigned long max_value, unsigned int step,
			  unsigned int flags ) {

	size_t dispaddr = ifr->ops_len;
	EFI_IFR_NUMERIC *numeric;
	EFI_IFR_NUMERIC_BASE *Base;
	unsigned int size;
	UINTN	ValueLen;

	ValueLen = mNumericDefaultTypeToWidth[flags & EFI_IFR_NUMERIC_SIZE];

	/* Add opcode */
	numeric = efi_ifr_op ( ifr, EFI_IFR_NUMERIC_OP, sizeof ( *Base )+ ValueLen);

	if ( ! numeric )
		return;

	numeric->Header.Scope = 1;

	numeric->Question.Header.Prompt = prompt_id;
	numeric->Question.Header.Help = help_id;
	numeric->Question.QuestionId = question_id;
	numeric->Question.VarStoreId = varstore_id;
	numeric->Question.VarStoreInfo.VarOffset = varstore_info;
	numeric->Question.Flags = vflags;
	numeric->Flags = flags;

	size = ( flags & EFI_IFR_NUMERIC_SIZE );

	switch ( size ) {
	case EFI_IFR_NUMERIC_SIZE_1 :
		numeric->data.u8.MinValue = min_value;
		numeric->data.u8.MaxValue = max_value;
		numeric->data.u8.Step = step;
		break;
	case EFI_IFR_NUMERIC_SIZE_2 :
		numeric->data.u16.MinValue = min_value;
		numeric->data.u16.MaxValue = max_value;
		numeric->data.u16.Step = step;
		break;
	case EFI_IFR_NUMERIC_SIZE_4 :
		numeric->data.u32.MinValue = min_value;
		numeric->data.u32.MaxValue = max_value;
		numeric->data.u32.Step = step;
		break;
	case EFI_IFR_NUMERIC_SIZE_8 :
		numeric->data.u64.MinValue = min_value;
		numeric->data.u64.MaxValue = max_value;
		numeric->data.u64.Step = step;
		break;
	}

	DBGC ( ifr, "IFR %p numeric prompt %#04x help %#04x question %#04x "
	       "varstore %#04x:%#04x\n", ifr, prompt_id, help_id, question_id,
	       varstore_id, varstore_info );
	DBGC2_HDA ( ifr, dispaddr, numeric, sizeof ( *numeric ) );
}

/**
 * Add string opcode to IFR builder
 *
 * @v ifr		IFR builder
 * @v prompt_id		Prompt string identifier
 * @v help_id		Help string identifier
 * @v question_id	Question identifier
 * @v varstore_id	Variable store identifier
 * @v varstore_info	Variable string identifier or offset
 * @v vflags		Variable flags
 * @v min_size		Minimum size
 * @v max_size		Maximum size
 * @v flags		Flags
 */
void efi_ifr_string_op ( struct efi_ifr_builder *ifr, unsigned int prompt_id,
			 unsigned int help_id, unsigned int question_id,
			 unsigned int varstore_id, unsigned int varstore_info,
			 unsigned int vflags, unsigned int min_size,
			 unsigned int max_size, unsigned int flags ) {
	size_t dispaddr = ifr->ops_len;
	EFI_IFR_STRING *string;

	/* Add opcode */
	string = efi_ifr_op ( ifr, EFI_IFR_STRING_OP, sizeof ( *string ) );
	if ( ! string )
		return;
	string->Question.Header.Prompt = prompt_id;
	string->Question.Header.Help = help_id;
	string->Question.QuestionId = question_id;
	string->Question.VarStoreId = varstore_id;
	string->Question.VarStoreInfo.VarOffset = varstore_info;
	string->Question.Flags = vflags;
	string->MinSize = min_size;
	string->MaxSize = max_size;
	string->Flags = flags;

	DBGC ( ifr, "IFR %p string prompt %#04x help %#04x question %#04x "
	       "varstore %#04x:%#04x\n", ifr, prompt_id, help_id, question_id,
	       varstore_id, varstore_info );
	DBGC2_HDA ( ifr, dispaddr, string, sizeof ( *string ) );
}

/**
 * Add suppress-if opcode to IFR builder
 *
 * @v ifr		IFR builder
 */
void efi_ifr_suppress_if_op ( struct efi_ifr_builder *ifr ) {
	size_t dispaddr = ifr->ops_len;
	EFI_IFR_SUPPRESS_IF *suppress_if;

	/* Add opcode */
	suppress_if = efi_ifr_op ( ifr, EFI_IFR_SUPPRESS_IF_OP,
				   sizeof ( *suppress_if ) );
	suppress_if->Header.Scope = 1;

	DBGC ( ifr, "IFR %p suppress-if\n", ifr );
	DBGC2_HDA ( ifr, dispaddr, suppress_if, sizeof ( *suppress_if ) );
}

/**
 * Add text opcode to IFR builder
 *
 * @v ifr		IFR builder
 * @v prompt_id		Prompt string identifier
 * @v help_id		Help string identifier
 * @v text_id		Text string identifier
 */
void efi_ifr_text_op ( struct efi_ifr_builder *ifr, unsigned int prompt_id,
		       unsigned int help_id, unsigned int text_id ) {
	size_t dispaddr = ifr->ops_len;
	EFI_IFR_TEXT *text;

	/* Add opcode */
	text = efi_ifr_op ( ifr, EFI_IFR_TEXT_OP, sizeof ( *text ) );
	if ( ! text )
		return;
	text->Statement.Prompt = prompt_id;
	text->Statement.Help = help_id;
	text->TextTwo = text_id;

	DBGC ( ifr, "IFR %p text prompt %#04x help %#04x text %#04x\n",
	       ifr, prompt_id, help_id, text_id );
	DBGC2_HDA ( ifr, dispaddr, text, sizeof ( *text ) );
}

/**
 * Add true opcode to IFR builder
 *
 * @v ifr		IFR builder
 */
void efi_ifr_true_op ( struct efi_ifr_builder *ifr ) {
	size_t dispaddr = ifr->ops_len;
	EFI_IFR_TRUE *true;

	/* Add opcode */
	true = efi_ifr_op ( ifr, EFI_IFR_TRUE_OP, sizeof ( *true ) );

	DBGC ( ifr, "IFR %p true\n", ifr );
	DBGC2_HDA ( ifr, dispaddr, true, sizeof ( *true ) );
}

unsigned int efi_ifr_varstore_op ( struct efi_ifr_builder *ifr,
                          const EFI_GUID *guid,
                          UINT16 size,
                          const char *name ) {

    UINT8	length;
    EFI_IFR_VARSTORE *varstore;

    length = strlen(&name[0]);
    length += sizeof ( *varstore );

    /* Add opcode */
    varstore = efi_ifr_op ( ifr, EFI_IFR_VARSTORE_OP,length );

    if ( ! varstore )
        return 0;

    varstore->VarStoreId = ++(ifr->varstore_id);
    memcpy ( &varstore->Guid, guid, sizeof ( varstore->Guid ) );
    varstore->Size = size;
    strcpy((char *)varstore->Name,name);
    return varstore->VarStoreId;
}

/**
 * Add name/value store opcode to IFR builder
 *
 * @v ifr		IFR builder
 * @v guid		GUID
 * @ret varstore_id	Variable store identifier, or 0 on failure
 */
unsigned int efi_ifr_varstore_name_value_op ( struct efi_ifr_builder *ifr,
					      const EFI_GUID *guid ) {
	size_t dispaddr = ifr->ops_len;
	EFI_IFR_VARSTORE_NAME_VALUE *varstore;

	/* Add opcode */
	varstore = efi_ifr_op ( ifr, EFI_IFR_VARSTORE_NAME_VALUE_OP,
				sizeof ( *varstore ) );
	if ( ! varstore )
		return 0;
	varstore->VarStoreId = ++(ifr->varstore_id);
	memcpy ( &varstore->Guid, guid, sizeof ( varstore->Guid ) );

	DBGC ( ifr, "IFR %p name/value store %#04x\n",
	       ifr, varstore->VarStoreId );
	DBGC2_HDA ( ifr, dispaddr, varstore, sizeof ( *varstore ) );
	return varstore->VarStoreId;
}

/**
 * Free memory used by IFR builder
 *
 * @v ifr		IFR builder
 */
void efi_ifr_free ( struct efi_ifr_builder *ifr ) {

	free ( ifr->ops );
	free ( ifr->strings );
	memset ( ifr, 0, sizeof ( *ifr ) );
}

/**
 * Construct package list from IFR builder
 *
 * @v ifr		IFR builder
 * @v guid		Package GUID
 * @v language		Language
 * @v language_id	Language string ID
 * @ret package		Package list, or NULL
 *
 * The package list is allocated using malloc(), and must eventually
 * be freed by the caller.  (The caller must also call efi_ifr_free()
 * to free the temporary storage used during construction.)
 */
EFI_HII_PACKAGE_LIST_HEADER * efi_ifr_package_ex (
  struct efi_ifr_builder *ifr,
  const EFI_GUID *guid,
  const char *language_eng,
  unsigned int language_eng_id,
  const char *language_uefi,    OPTIONAL
  unsigned int language_uefi_id OPTIONAL
)
{

    struct {
        EFI_HII_PACKAGE_LIST_HEADER header;
        struct {
            EFI_HII_PACKAGE_HEADER header;
            uint8_t data[ifr->ops_len];
        } __attribute__ (( packed )) ops;
        struct {
            union {
                EFI_HII_STRING_PACKAGE_HDR header;
                uint8_t pad[ offsetof(EFI_HII_STRING_PACKAGE_HDR,Language) + strlen ( language_eng ) + 1 /* NUL */ ];
            } __attribute__ (( packed )) header;
            uint8_t data[ifr->strings_len];
            EFI_HII_STRING_BLOCK end;
        } __attribute__ (( packed )) strings_eng;
#ifdef XUEFI_LANG_SUPPORT
        struct {
            union {
                EFI_HII_STRING_PACKAGE_HDR header;
                uint8_t pad[offsetof(EFI_HII_STRING_PACKAGE_HDR,Language) + strlen ( language_uefi ) + 1 /* NUL */ ];
            } __attribute__ (( packed )) header;
            uint8_t data[ifr->uefi_strings_len];
            EFI_HII_STRING_BLOCK end;
        } __attribute__ (( packed )) strings_uefi;
#endif
        EFI_HII_PACKAGE_HEADER end;
    } __attribute__ (( packed )) *package;

    /* Fail if any previous allocation failed */
    if ( ifr->failed )
        return NULL;

    /* Allocate package list */
    package = zalloc ( sizeof ( *package ) );
    if ( ! package )
        return NULL;

    /* Populate package list */
    package->header.PackageLength = sizeof ( *package );
    memcpy ( &package->header.PackageListGuid, guid,sizeof ( package->header.PackageListGuid ) );

    package->ops.header.Length = sizeof ( package->ops );
    package->ops.header.Type = EFI_HII_PACKAGE_FORMS;
    memcpy ( package->ops.data, ifr->ops, sizeof ( package->ops.data ) );

    // English String Package
    package->strings_eng.header.header.Header.Length = sizeof ( package->strings_eng );
    package->strings_eng.header.header.Header.Type = EFI_HII_PACKAGE_STRINGS;
    package->strings_eng.header.header.HdrSize = sizeof ( package->strings_eng.header );
    package->strings_eng.header.header.StringInfoOffset = sizeof ( package->strings_eng.header );
    package->strings_eng.header.header.LanguageName = language_eng_id;
    strcpy ( package->strings_eng.header.header.Language, language_eng );
    memcpy ( package->strings_eng.data, ifr->strings,sizeof ( package->strings_eng.data ) );
    package->strings_eng.end.BlockType = EFI_HII_SIBT_END;

    // x-UEFI String Package
#ifdef XUEFI_LANG_SUPPORT
    package->strings_uefi.header.header.Header.Length = sizeof ( package->strings_uefi );
    package->strings_uefi.header.header.Header.Type = EFI_HII_PACKAGE_STRINGS;
    package->strings_uefi.header.header.HdrSize = sizeof ( package->strings_uefi.header );
    package->strings_uefi.header.header.StringInfoOffset = sizeof ( package->strings_uefi.header );
    package->strings_uefi.header.header.LanguageName = language_uefi_id;
    strcpy ( package->strings_uefi.header.header.Language, language_uefi );
    memcpy ( package->strings_uefi.data, ifr->uefi_strings,sizeof ( package->strings_uefi.data ) );
    package->strings_uefi.end.BlockType = EFI_HII_SIBT_END;
#endif

    package->end.Type = EFI_HII_PACKAGE_END;
    package->end.Length = sizeof ( package->end );

    return &package->header;
}

/**
 * Construct package list from IFR builder
 *
 * @v ifr		IFR builder
 * @v guid		Package GUID
 * @v language		Language
 * @v language_id	Language string ID
 * @ret package		Package list, or NULL
 *
 * The package list is allocated using malloc(), and must eventually
 * be freed by the caller.  (The caller must also call efi_ifr_free()
 * to free the temporary storage used during construction.)
 */
EFI_HII_PACKAGE_LIST_HEADER * efi_ifr_package ( struct efi_ifr_builder *ifr,
						const EFI_GUID *guid,
						const char *language,
						unsigned int language_id ) {
	struct {
		EFI_HII_PACKAGE_LIST_HEADER header;
		struct {
			EFI_HII_PACKAGE_HEADER header;
			uint8_t data[ifr->ops_len];
		} __attribute__ (( packed )) ops;
		struct {
			union {
				EFI_HII_STRING_PACKAGE_HDR header;
				uint8_t pad[offsetof(EFI_HII_STRING_PACKAGE_HDR,
						     Language) +
					    strlen ( language ) + 1 /* NUL */ ];
			} __attribute__ (( packed )) header;
			uint8_t data[ifr->strings_len];
			EFI_HII_STRING_BLOCK end;
		} __attribute__ (( packed )) strings;
		EFI_HII_PACKAGE_HEADER end;
	} __attribute__ (( packed )) *package;

	/* Fail if any previous allocation failed */
	if ( ifr->failed )
		return NULL;

	/* Allocate package list */
	package = zalloc ( sizeof ( *package ) );
	if ( ! package )
		return NULL;

	/* Populate package list */
	package->header.PackageLength = sizeof ( *package );
	memcpy ( &package->header.PackageListGuid, guid,
		 sizeof ( package->header.PackageListGuid ) );
	package->ops.header.Length = sizeof ( package->ops );
	package->ops.header.Type = EFI_HII_PACKAGE_FORMS;
	memcpy ( package->ops.data, ifr->ops, sizeof ( package->ops.data ) );
	package->strings.header.header.Header.Length =
		sizeof ( package->strings );
	package->strings.header.header.Header.Type =
		EFI_HII_PACKAGE_STRINGS;
	package->strings.header.header.HdrSize =
		sizeof ( package->strings.header );
	package->strings.header.header.StringInfoOffset =
		sizeof ( package->strings.header );
	package->strings.header.header.LanguageName = language_id;
	strcpy ( package->strings.header.header.Language, language );
	memcpy ( package->strings.data, ifr->strings,
		 sizeof ( package->strings.data ) );
	package->strings.end.BlockType = EFI_HII_SIBT_END;
	package->end.Type = EFI_HII_PACKAGE_END;
	package->end.Length = sizeof ( package->end );

	return &package->header;
}

