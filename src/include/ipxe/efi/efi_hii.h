#ifndef _IPXE_EFI_HII_H
#define _IPXE_EFI_HII_H

/** @file
 *
 * EFI human interface infrastructure
 */

FILE_LICENCE ( GPL2_OR_LATER_OR_UBDL );

#include <string.h>
#include <ipxe/efi/Uefi/UefiInternalFormRepresentation.h>
#include <ipxe/efi/Guid/MdeModuleHii.h>

/** GUID indicating formset compliance for IBM Unified Configuration Manager */
#define EFI_HII_IBM_UCM_COMPLIANT_FORMSET_GUID				   \
	{ 0x5c8e9746, 0xa5f7, 0x4593,					   \
	  { 0xaf, 0x1f, 0x66, 0xa8, 0x2a, 0xa1, 0x9c, 0xb1 } }

typedef union {
  struct {
	UINT8 FormActionId;
	UINT8 FormId;
  } DecodedAction;
  UINT16 QuestionId;
} HiiActionKey;

/** An EFI IFR builder */
struct efi_ifr_builder {
	/** IFR opcodes */
	EFI_IFR_OP_HEADER *ops;
	/** Length of IFR opcodes */
	size_t ops_len;
	/** Strings */
	EFI_HII_STRING_BLOCK *strings;
	/** Length of strings */
	size_t strings_len;
	/** Current string identifier */
	unsigned int string_id;
	/** x-uefi Strings */
	EFI_HII_STRING_BLOCK *uefi_strings;
	/** Length of x-uefi strings */
	size_t uefi_strings_len;
	/** x-uefi string skip cnt */
	unsigned int uefi_string_skip_cnt;
	/** x-uefi string index */
	unsigned int uefi_string_index;
	/** Current variable store identifier */
	unsigned int varstore_id;
	/** Current form identifier */
	unsigned int form_id;
	/** An allocation has failed */
	int failed;
};

/**
 * Initialise IFR builder
 *
 * @v ifr		IFR builder
 *
 * The caller must eventually call efi_ifr_free() to free the dynamic
 * storage associated with the IFR builder.
 */
static inline void efi_ifr_init ( struct efi_ifr_builder *ifr ) {
	memset ( ifr, 0, sizeof ( *ifr ) );
}

extern unsigned int
UnicodeSPrint (
  OUT CHAR16        *Buffer,
  IN  UINTN         BufferSize,
  const char        *fmt,
  ...
  );

extern void
efi_ifr_OneOf_Op (  struct efi_ifr_builder *ifr,
  EFI_QUESTION_ID  QuestionId,
  EFI_VARSTORE_ID  VarStoreId,
  UINT16           VarInfo,
  EFI_STRING_ID    Prompt,
  EFI_STRING_ID    Help,
  UINT8            QuestionFlags,
  UINT8            OneOfFlags
  );

extern void
efi_ifr_oneofoption_op ( struct efi_ifr_builder *ifr,
  IN UINT16  StringId,
  IN UINT8   Flags,
  IN UINT8   Type,
  IN UINT64  Value
  );

void efi_ifr_suppress_grayout_op ( struct efi_ifr_builder *ifr,
                                   UINT16       QuestionId,
                                   UINT16       Value,
                                   BOOLEAN      Suppress
);

extern unsigned int efi_ifr_string ( struct efi_ifr_builder *ifr,
				     const char *fmt, ... );

extern unsigned int efi_ifr_string_xuefi ( struct efi_ifr_builder *ifr,
					 EFI_STRING_ID prompt_id,
					 const char *fmt, ... );

extern void efi_ifr_end_op ( struct efi_ifr_builder *ifr );
extern void efi_ifr_false_op ( struct efi_ifr_builder *ifr );
extern unsigned int efi_ifr_form_op ( struct efi_ifr_builder *ifr,
				      unsigned int title_id );

extern unsigned int efi_ifr_form_op_ex ( struct efi_ifr_builder *ifr,
                   unsigned int form_id,
                   unsigned int title_id );

extern void efi_ifr_form_set_op ( struct efi_ifr_builder *ifr,
				  const EFI_GUID *guid,
				  unsigned int title_id, unsigned int help_id,
				  ... );
void efi_ifr_get_op ( struct efi_ifr_builder *ifr, unsigned int varstore_id,
		      unsigned int varstore_info, unsigned int varstore_type );
extern void efi_ifr_guid_class_op ( struct efi_ifr_builder *ifr,
				    unsigned int class );
extern void efi_ifr_guid_subclass_op ( struct efi_ifr_builder *ifr,
				       unsigned int subclass );
extern void efi_ifr_numeric_op ( struct efi_ifr_builder *ifr,
				 unsigned int prompt_id,
				 unsigned int help_id, unsigned int question_id,
				 unsigned int varstore_id,
				 unsigned int varstore_info,
				 unsigned int vflags, unsigned long min_value,
				 unsigned long max_value, unsigned int step,
				 unsigned int flags );
extern void efi_ifr_string_op ( struct efi_ifr_builder *ifr,
				unsigned int prompt_id, unsigned int help_id,
				unsigned int question_id,
				unsigned int varstore_id,
				unsigned int varstore_info, unsigned int vflags,
				unsigned int min_size, unsigned int max_size,
				unsigned int flags );

extern void efi_ifr_goto_op ( struct efi_ifr_builder *ifr,
				 unsigned int form_id,
				 unsigned int Promt,
				 unsigned int Help,
				 unsigned int QuestionFlags,
				 unsigned int QuestionId );

extern void efi_ifr_checkbox_op ( struct efi_ifr_builder *ifr,
				 unsigned int prompt_id,unsigned int help_id,
				 unsigned int question_id,
				 unsigned int varstore_id,
				 unsigned int varstore_info,
				 unsigned int varstore_offset,
				 unsigned int vflags,
				 unsigned int flags );

extern unsigned int efi_ifr_varstore_op ( struct efi_ifr_builder *ifr,
				 const EFI_GUID *guid,
				 UINT16 size,
				 const char *name );

extern void efi_ifr_suppress_if_op ( struct efi_ifr_builder *ifr );
extern void efi_ifr_text_op ( struct efi_ifr_builder *ifr,
			      unsigned int prompt_id, unsigned int help_id,
			      unsigned int text_id );
extern void efi_ifr_true_op ( struct efi_ifr_builder *ifr );
extern unsigned int
efi_ifr_varstore_name_value_op ( struct efi_ifr_builder *ifr,
				 const EFI_GUID *guid );
extern void efi_ifr_free ( struct efi_ifr_builder *ifr );
extern EFI_HII_PACKAGE_LIST_HEADER *
efi_ifr_package ( struct efi_ifr_builder *ifr, const EFI_GUID *guid,
		  const char *language, unsigned int language_id );

extern EFI_HII_PACKAGE_LIST_HEADER * efi_ifr_package_ex (
  struct efi_ifr_builder *ifr,
  const EFI_GUID *guid,
  const char *language_eng,
  unsigned int language_eng_id,
  const char *language_uefi,  OPTIONAL
  unsigned int language_uefi_id  OPTIONAL
);

extern EFI_STRING
EFIAPI
HiiGetString (
  IN EFI_HII_HANDLE  HiiHandle,
  IN EFI_STRING_ID   StringId,
  IN OUT UINT8       *StringLen,
  IN CONST CHAR8     *Language  OPTIONAL
  );

extern VOID *
EFIAPI
HiiAllocateOpCodeHandle (
  VOID
  );

#endif /* _IPXE_EFI_HII_H */
