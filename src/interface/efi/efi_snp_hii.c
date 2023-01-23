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

/**
 * @file
 *
 * EFI SNP HII protocol
 *
 * The HII protocols are some of the less-well designed parts of the
 * entire EFI specification.  This is a significant accomplishment.
 *
 * The face-slappingly ludicrous query string syntax seems to be
 * motivated by the desire to allow a caller to query multiple drivers
 * simultaneously via the single-instance HII_CONFIG_ROUTING_PROTOCOL,
 * which is supposed to pass relevant subsets of the query string to
 * the relevant drivers.
 *
 * Nobody uses the HII_CONFIG_ROUTING_PROTOCOL.  Not even the EFI
 * setup browser uses the HII_CONFIG_ROUTING_PROTOCOL.  To the best of
 * my knowledge, there has only ever been one implementation of the
 * HII_CONFIG_ROUTING_PROTOCOL (as part of EDK2), and it just doesn't
 * work.  It's so badly broken that I can't even figure out what the
 * code is _trying_ to do.
 *
 * Fundamentally, the problem seems to be that Javascript programmers
 * should not be allowed to design APIs for C code.
 */

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include <wchar.h>
#include <errno.h>
#ifdef PEN_IONIC_EFIROM
#include <drivers/net/ionic.h>
#endif
#include <ipxe/settings.h>
#include <ipxe/nvo.h>
#include <ipxe/device.h>
#include <ipxe/netdevice.h>
#include <ipxe/version.h>
#include <ipxe/efi/efi.h>
#include <ipxe/efi/efi_hii.h>
#include <ipxe/efi/efi_snp.h>
#include <ipxe/efi/efi_strings.h>
#include <ipxe/efi/efi_path.h>
#include <ipxe/efi/efi_utils.h>
#include <ipxe/efi/efi_null.h>
#include <config/branding.h>
#include <ipxe/pci.h>
#include <ipxe/efi/efi_ionic.h>
#include <ipxe/efi/efi_driver.h>
#include <ipxe/efi/Protocol/HiiPopup.h>

/** EFI platform setup formset GUID */
static EFI_GUID efi_hii_platform_setup_formset_guid
	= EFI_HII_PLATFORM_SETUP_FORMSET_GUID;

/** EFI IBM UCM compliant formset GUID */
static EFI_GUID efi_hii_ibm_ucm_compliant_formset_guid
	= EFI_HII_IBM_UCM_COMPLIANT_FORMSET_GUID;

/** EFI HII database protocol */
static EFI_HII_DATABASE_PROTOCOL *efihii;
EFI_REQUEST_PROTOCOL ( EFI_HII_DATABASE_PROTOCOL, &efihii );

#ifdef PEN_IONIC_EFIROM
NIC_HII_PACKAGE_INFO    *Nic = NULL;
IONIC_NIC_HII_INFO      *NicHii = NULL;
EFI_HII_HANDLE          HiiHandle = NULL;
int                     fetch_number;
int                     fetch_len;
UINT16                  LastLnkStatus = 0;
#endif

/**
 * Identify settings to be exposed via HII
 *
 * @v snpdev		SNP device
 * @ret settings	Settings, or NULL
 */
static struct settings * efi_snp_hii_settings ( struct efi_snp_device *snpdev ){

	return find_child_settings ( netdev_settings ( snpdev->netdev ),
				     NVO_SETTINGS_NAME );
}

/**
 * Check whether or not setting is applicable
 *
 * @v snpdev		SNP device
 * @v setting		Setting
 * @ret applies		Setting applies
 */
static int efi_snp_hii_setting_applies ( struct efi_snp_device *snpdev,
					 struct setting *setting ) {

	return nvo_applies ( efi_snp_hii_settings ( snpdev ), setting );
}

/**
 * Generate a random GUID
 *
 * @v guid		GUID to fill in
 */
static void efi_snp_hii_random_guid ( EFI_GUID *guid ) {
	uint8_t *byte = ( ( uint8_t * ) guid );
	unsigned int i;

	for ( i = 0 ; i < sizeof ( *guid ) ; i++ )
		*(byte++) = random();
}

#ifdef PEN_IONIC_EFIROM
static void ionic_hii_process( struct efi_ifr_builder *ifr ,UINT16 FormId,UINT8 Cmd) {

    UINT8	i;
    NIC_HII_STRUC	*NicHiiPtr = (NIC_HII_STRUC *)Nic->NicHiiInfo;
    CHAR8	*Buffer = NULL;
    UINT8	BufferLen = 0;
    EFI_STATUS	Status = EFI_NOT_FOUND;
    NIC_HII_STRUC	*Ptr;
    UINT16	QuestionBaseKey = 0;
    UINT8	ControlFlag = 0;

    if(NicHiiPtr == NULL) return;

    QuestionBaseKey = Nic->SnpQuestionBaseKey;

    for(i = 0;i < (sizeof(IONIC_NIC_HII_INFO)/sizeof(NIC_HII_STRUC)) ;i++,NicHiiPtr++){
        if(Cmd == HII_BUILD_STRING){
            if(i == 0) continue;	//skip the entry Nic string

            if(NicHiiPtr->PromptStr) NicHiiPtr->Hii.Prompt = efi_ifr_string( ifr, "%s", NicHiiPtr->PromptStr );

            if(NicHiiPtr->xUefiStr) NicHiiPtr->Hii.xUefi = efi_ifr_string_xuefi( ifr, NicHiiPtr->Hii.Prompt , "%s", NicHiiPtr->xUefiStr );

            if(NicHiiPtr->HelpStr) NicHiiPtr->Hii.Help = efi_ifr_string( ifr, "%s", NicHiiPtr->HelpStr );

            if(NicHiiPtr->WarningStr) NicHiiPtr->Hii.Warning = efi_ifr_string( ifr, "%s", NicHiiPtr->WarningStr );

            if(NicHiiPtr->ValueStr){
                // check if need to update value string from callback function
                if(NicHiiPtr->Callback) Status = NicHiiPtr->Callback(0,&Buffer,&BufferLen);

                if(Status == EFI_SUCCESS) {
                    NicHiiPtr->Hii.TextTwo = efi_ifr_string( ifr, "%s", Buffer );
                }else{
                    NicHiiPtr->Hii.TextTwo = efi_ifr_string( ifr, "%s", NicHiiPtr->ValueStr );
                }
                Status = EFI_NOT_FOUND;
            }
        } else if (Cmd == HII_BUILD_VARSTORE) {
            if((NicHiiPtr->Type == EFI_IFR_NUMERIC_OP) || (NicHiiPtr->Type == EFI_IFR_STRING_OP) \
                || (NicHiiPtr->Type == EFI_IFR_CHECKBOX_OP) || (NicHiiPtr->Type == EFI_IFR_ONE_OF_OP)) {
                if(NicHiiPtr->Show == TRUE){
                    efi_snp_hii_random_guid ( &NicHiiPtr->VarstoreGuid );
                    NicHiiPtr->VarStoreId = efi_ifr_varstore_op ( ifr,
                                             &NicHiiPtr->VarstoreGuid,
                                             NicHiiPtr->VarStoreSize,
                                             NicHiiPtr->Setting->name);
                }
            }
        } else if( (Cmd == HII_BUILD_FORM) &&
                   (NicHiiPtr->Type == EFI_IFR_REF_OP) ) {
            efi_ifr_goto_op (ifr,
                             NicHiiPtr->FormId,
                             NicHiiPtr->Hii.Prompt,
                             NicHiiPtr->Hii.Help,
                             EFI_IFR_FLAG_CALLBACK,
                             NicHiiPtr->FormId);
        } else if( (NicHiiPtr->FormId == FormId) &&
                   (Cmd == HII_BUILD_ITEM) &&
                   (NicHiiPtr->Type == EFI_IFR_TEXT_OP) &&
                   (NicHiiPtr->Show == TRUE)){

            if(NicHiiPtr->Suppress == 1){
                efi_ifr_suppress_if_op(ifr);
                efi_ifr_true_op(ifr);
            }

            efi_ifr_text_op ( ifr,
                                NicHiiPtr->Hii.Prompt,
                                NicHiiPtr->Hii.Help,
                                NicHiiPtr->Hii.TextTwo
                            );

            if(NicHiiPtr->Suppress == 1)
                efi_ifr_end_op ( ifr );

        } else if( (NicHiiPtr->FormId == FormId) &&
                   (Cmd == HII_BUILD_ITEM) &&
                   (NicHiiPtr->Type == EFI_IFR_NUMERIC_OP) &&
                   (NicHiiPtr->Show == TRUE)){
            if(NicHiiPtr->ControlId != 0){
                efi_ifr_suppress_grayout_op (ifr,
                                             NicHiiPtr->ControlId + QuestionBaseKey,
                                             0,        //value
                                             0         //0: grayout 1: supress
                                            );
            }
            efi_ifr_numeric_op( ifr,
                                NicHiiPtr->Hii.Prompt,
                                NicHiiPtr->Hii.Help,
                                NicHiiPtr->Setting->tag + QuestionBaseKey,		//plus DeviceIndex
                                NicHiiPtr->VarStoreId,
                                0,
                                EFI_IFR_FLAG_CALLBACK,
                                0,
                                4094,
                                1,
                                EFI_IFR_NUMERIC_SIZE_2 | EFI_IFR_DISPLAY_UINT_DEC);
            efi_ifr_end_op ( ifr );

            if(NicHiiPtr->ControlId != 0) efi_ifr_end_op ( ifr );

        } else if ( (NicHiiPtr->FormId == FormId) &&
                    (Cmd == HII_BUILD_ITEM) &&
                    (NicHiiPtr->Type == EFI_IFR_STRING_OP) &&
                    (NicHiiPtr->Show == TRUE)){

            if(NicHiiPtr->ControlId != 0){
                efi_ifr_suppress_grayout_op (ifr,
                                             NicHiiPtr->ControlId + QuestionBaseKey,
                                             0,		//value
                                             0			//0: grayout 1: supress
                                            );
            }

            efi_ifr_string_op ( ifr,
                                NicHiiPtr->Hii.Prompt,
                                NicHiiPtr->Hii.Help,
                                NicHiiPtr->Setting->tag + QuestionBaseKey,		//plus DeviceIndex
                                NicHiiPtr->VarStoreId,
                                0,
                                EFI_IFR_FLAG_CALLBACK, 0x00, 0xff, 1
                              );

            if(NicHiiPtr->ControlId != 0) efi_ifr_end_op ( ifr );

        } else if ( (NicHiiPtr->FormId == FormId) &&
                    (Cmd == HII_BUILD_ITEM) &&
                    (NicHiiPtr->Type == EFI_IFR_CHECKBOX_OP) &&
                    (NicHiiPtr->Show == TRUE)){
            if(NicHiiPtr->ControlId != 0){

                efi_ifr_suppress_grayout_op ( ifr,
                                              NicHiiPtr->ControlId + QuestionBaseKey,
                                              0,		//value
                                              0			//0: grayout 1: supress
                                            );
            }

            if(NicHiiPtr->Suppress == 1){
                efi_ifr_suppress_if_op(ifr);
                efi_ifr_true_op(ifr);
            }

            efi_ifr_checkbox_op (ifr,
                                 NicHiiPtr->Hii.Prompt,
                                 NicHiiPtr->Hii.Help,
                                 NicHiiPtr->Setting->tag + QuestionBaseKey,		//plus DeviceIndex
                                 NicHiiPtr->VarStoreId,
                                 0,
                                 0,
                                 EFI_IFR_FLAG_CALLBACK,
                                 1);

            if(NicHiiPtr->Suppress == 1) efi_ifr_end_op ( ifr );

            if(NicHiiPtr->ControlId != 0) efi_ifr_end_op ( ifr );

        } else if ( (NicHiiPtr->FormId == FormId) &&
                    (Cmd == HII_BUILD_ITEM) &&
                    (NicHiiPtr->Type == EFI_IFR_ONE_OF_OP) &&
                    (NicHiiPtr->Show == TRUE)){

            if(NicHiiPtr->ControlId != 0){
                ControlFlag = 1;
                efi_ifr_suppress_grayout_op ( ifr,
                                                  NicHiiPtr->ControlId + QuestionBaseKey,
                                                  0,		//value
                                                  0			//0: grayout 1: supress
                                            );
            }

            efi_ifr_OneOf_Op ( ifr,
                                NicHiiPtr->Setting->tag + QuestionBaseKey,			//plus DeviceIndex
                                NicHiiPtr->VarStoreId,
                                0,
                                NicHiiPtr->Hii.Prompt,
                                NicHiiPtr->Hii.Help,
                                EFI_IFR_FLAG_CALLBACK,
                                EFI_IFR_NUMERIC_SIZE_1
                              );

            // looking for all of next one of option field
            Ptr = NicHiiPtr;
            Ptr++;

            while (Ptr->Type == EFI_IFR_ONE_OF_OPTION_OP) {
                if(Ptr->Show == TRUE){
                    efi_ifr_oneofoption_op ( ifr,
                                            Ptr->Hii.Prompt,
                                            Ptr->Flag,
                                            EFI_IFR_NUMERIC_SIZE_1,
                                            Ptr->Value );
                }
                Ptr++;
                NicHiiPtr++;
                i++;
            }
            efi_ifr_end_op ( ifr );

            if(ControlFlag == 1){
                efi_ifr_end_op ( ifr );
                ControlFlag = 0;
            }
        }
    }
}

/**
 * Build HII package list for SNP device
 *
 * @v snpdev		SNP device
 * @ret package		Package list, or NULL on error
 */
static EFI_HII_PACKAGE_LIST_HEADER *
efi_snp_hii_package_list_ionic ( struct efi_snp_device *snpdev ) {

    struct net_device *netdev = snpdev->netdev;
    struct ionic *ionic = netdev->priv;
    struct lif *lif = ionic->lif;
    struct efi_ifr_builder ifr;
    EFI_HII_PACKAGE_LIST_HEADER *package;
    const char *name;
    UINT8 length = 0;
    int FormPrompt;

    // Create new ionic package
    if(ionic_hii_valid((void *)snpdev) != 0) return NULL;

    Nic = (NIC_HII_PACKAGE_INFO *)ionic_hii_init((void *)snpdev);
    NicHii = (IONIC_NIC_HII_INFO *)Nic->NicHiiInfo;

    /* Initialise IFR builder */
    efi_ifr_init ( &ifr );

    /* Generate GUIDs */
    efi_snp_hii_random_guid ( &Nic->PackageGuid );
    efi_snp_hii_random_guid ( &Nic->FormsetGuid );

    /* Init ionic String Id */
    ionic_hii_process( &ifr, 0 ,HII_BUILD_STRING );

    /* Generate title string (used more than once) */
    if(NicHii->DevName.Callback(0, (CHAR8 **)&name,&length) != EFI_SUCCESS){

        name = ( product_name[0] ? product_name : product_short_name );
    }

    NicHii->NicName.Hii.Prompt = efi_ifr_string ( &ifr, "%s (%s)", name,netdev_addr ( netdev ) );

    // build form prompt for both en-us and x-uefi
    FormPrompt = NicHii->NicName.Hii.Prompt;
    efi_ifr_string_xuefi( &ifr, FormPrompt ,"VndrFormSet" );

    // construct boot menu entry name
    efi_snprintf ( snpdev->controller_name,( sizeof ( snpdev->controller_name ) /sizeof ( snpdev->controller_name[0] ) ),\
                                            "%s (%s)",\
                                            name,netdev_addr ( netdev ) );

    /* Generate opcodes */
    efi_ifr_form_set_op (&ifr,
                        &Nic->FormsetGuid,
                        NicHii->NicName.Hii.Prompt,
                        efi_ifr_string ( &ifr, "Configure %s",product_short_name ),
                        &efi_hii_platform_setup_formset_guid,
                        &efi_hii_ibm_ucm_compliant_formset_guid,
                        NULL );

    efi_ifr_guid_class_op ( &ifr, EFI_NETWORK_DEVICE_CLASS );
    efi_ifr_guid_subclass_op ( &ifr, EFI_SINGLE_USE_SUBCLASS );


    ionic_hii_process( &ifr, 0 ,HII_BUILD_VARSTORE );
    // check oob_cap bit
    if( lif->oob_cap == 0){
        NicHii->OutofBandManage.Show = FALSE;
        NicHii->OutofBandManage.Flag = 0;
        NicHii->SideBandInterface.Flag = EFI_IFR_OPTION_DEFAULT;
    }

    // build form prompt for both en-us and x-uefi
    FormPrompt = efi_ifr_string( &ifr, "Main Configuration Page" );
    efi_ifr_string_xuefi( &ifr, FormPrompt ,"VndrConfigPage" );
    //begin of form1
    efi_ifr_form_op_ex ( &ifr, NicHii->NicName.FormId, FormPrompt );

    ionic_hii_process( &ifr, 0 ,HII_BUILD_FORM );
    ionic_hii_process( &ifr, NicHii->NicName.FormId, HII_BUILD_ITEM );
    efi_ifr_end_op ( &ifr );
    //end of form1
    ionic_checkpoint_cb(netdev, IONIC_OPROM_REGISTER_FORM1_DONE);
    // build form prompt for both en-us and x-uefi
    FormPrompt = efi_ifr_string( &ifr, "Firmware Image Properties" );
    efi_ifr_string_xuefi( &ifr, FormPrompt ,"FrmwImgMenu" );

    //begin of form2
    efi_ifr_form_op_ex ( &ifr, NicHii->FirmwareInfo.FormId, FormPrompt );
    ionic_hii_process( &ifr, NicHii->FirmwareInfo.FormId, HII_BUILD_ITEM );
    efi_ifr_end_op ( &ifr );
    // end of form2
    ionic_checkpoint_cb(netdev, IONIC_OPROM_REGISTER_FORM2_DONE);
    // build form prompt for both en-us and x-uefi
    FormPrompt = efi_ifr_string( &ifr, "NIC Configuration" );
    efi_ifr_string_xuefi( &ifr, FormPrompt ,"NICConfig" );

    // begin of form3
    efi_ifr_form_op_ex ( &ifr, NicHii->NicConfig.FormId, FormPrompt );
    ionic_hii_process( &ifr, NicHii->NicConfig.FormId, HII_BUILD_ITEM );
    efi_ifr_end_op ( &ifr );
    // end of form3
    ionic_checkpoint_cb(netdev, IONIC_OPROM_REGISTER_FORM3_DONE);
    // build form prompt for both en-us and x-uefi
    FormPrompt = efi_ifr_string( &ifr, "Device Level Configuration" );
    efi_ifr_string_xuefi( &ifr, FormPrompt ,"DeviceLevelConfig" );

    // begin of form4
    efi_ifr_form_op_ex ( &ifr, NicHii->DevLevel.FormId, FormPrompt );
    ionic_hii_process( &ifr, NicHii->DevLevel.FormId, HII_BUILD_ITEM );
    efi_ifr_end_op ( &ifr );
    // end of form4
    ionic_checkpoint_cb(netdev, IONIC_OPROM_REGISTER_FORM4_DONE);

    efi_ifr_end_op ( &ifr );
    //end of formset

    /* Build package */
    package = efi_ifr_package_ex ( &ifr,
                                   &Nic->PackageGuid,
                                   "en-US",
                                   efi_ifr_string ( &ifr, "English" ),
                                   "x-UEFI",
                                   efi_ifr_string ( &ifr, "x-UEFI" ));

    if ( ! package ) {
        DBGC ( snpdev, "SNPDEV %p could not build IFR package\n",
                snpdev );
            efi_ifr_free ( &ifr );
        return NULL;
    }
    ionic_checkpoint_cb(netdev, IONIC_OPROM_REGISTER_HII_DONE);

    /* Free temporary storage */
    efi_ifr_free ( &ifr );
    return package;
}

void uninstall_ionic_hii () {

    EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
    EFI_STATUS efirc;

    efirc = bs->UninstallProtocolInterface (
                Nic->Handle, &gEfiionicHiiPackageInfoProtocol,
                Nic );
    if(efirc != EFI_SUCCESS) {
        DBG2("Error uninstalling hii protocol %llx\n", efirc);
    }

    if(Nic->NicHiiInfo){
        bs->FreePool ( Nic->NicHiiInfo );
        Nic->NicHiiInfo = NULL;
    }
}

#else

/**
 * Generate EFI SNP questions
 *
 * @v snpdev		SNP device
 * @v ifr		IFR builder
 * @v varstore_id	Variable store identifier
 */
static void efi_snp_hii_questions ( struct efi_snp_device *snpdev,
				    struct efi_ifr_builder *ifr,
				    unsigned int varstore_id ) {
	struct setting *setting;
	struct setting *previous = NULL;
	unsigned int name_id;
	unsigned int prompt_id;
	unsigned int help_id;
	unsigned int question_id;

	/* Add all applicable settings */
	for_each_table_entry ( setting, SETTINGS ) {
		if ( ! efi_snp_hii_setting_applies ( snpdev, setting ) )
			continue;
		if ( previous && ( setting_cmp ( setting, previous ) == 0 ) )
			continue;
		previous = setting;
		name_id = efi_ifr_string ( ifr, "%s", setting->name );
		prompt_id = efi_ifr_string ( ifr, "%s", setting->description );
		help_id = efi_ifr_string ( ifr, PRODUCT_SETTING_URI,
					   setting->name );
		question_id = setting->tag;
		efi_ifr_string_op ( ifr, prompt_id, help_id,
				    question_id, varstore_id, name_id,
				    0, 0x00, 0xff, 0 );
	}
}

/**
 * Build HII package list for SNP device
 *
 * @v snpdev		SNP device
 * @ret package		Package list, or NULL on error
 */
static EFI_HII_PACKAGE_LIST_HEADER *
efi_snp_hii_package_list ( struct efi_snp_device *snpdev ) {
	struct net_device *netdev = snpdev->netdev;
	struct device *dev = netdev->dev;
	struct efi_ifr_builder ifr;
	EFI_HII_PACKAGE_LIST_HEADER *package;
	const char *name;
	EFI_GUID package_guid;
	EFI_GUID formset_guid;
	EFI_GUID varstore_guid;
	unsigned int title_id;
	unsigned int varstore_id;

	/* Initialise IFR builder */
	efi_ifr_init ( &ifr );

	/* Determine product name */
	name = ( product_name[0] ? product_name : product_short_name );

	/* Generate GUIDs */
	efi_snp_hii_random_guid ( &package_guid );
	efi_snp_hii_random_guid ( &formset_guid );
	efi_snp_hii_random_guid ( &varstore_guid );

	/* Generate title string (used more than once) */
	title_id = efi_ifr_string ( &ifr, "%s (%s)", name,
				    netdev_addr ( netdev ) );

	/* Generate opcodes */
	efi_ifr_form_set_op ( &ifr, &formset_guid, title_id,
			      efi_ifr_string ( &ifr, "Configure %s",
					       product_short_name ),
			      &efi_hii_platform_setup_formset_guid,
			      &efi_hii_ibm_ucm_compliant_formset_guid, NULL );
	efi_ifr_guid_class_op ( &ifr, EFI_NETWORK_DEVICE_CLASS );
	efi_ifr_guid_subclass_op ( &ifr, 0x03 );
	varstore_id = efi_ifr_varstore_name_value_op ( &ifr, &varstore_guid );
	efi_ifr_form_op ( &ifr, title_id );
	efi_ifr_text_op ( &ifr,
			  efi_ifr_string ( &ifr, "Name" ),
			  efi_ifr_string ( &ifr, "Firmware product name" ),
			  efi_ifr_string ( &ifr, "%s", name ) );
	efi_ifr_text_op ( &ifr,
			  efi_ifr_string ( &ifr, "Version" ),
			  efi_ifr_string ( &ifr, "Firmware version" ),
			  efi_ifr_string ( &ifr, "%s", product_version ) );
	efi_ifr_text_op ( &ifr,
			  efi_ifr_string ( &ifr, "Driver" ),
			  efi_ifr_string ( &ifr, "Firmware driver" ),
			  efi_ifr_string ( &ifr, "%s", dev->driver_name ) );
	efi_ifr_text_op ( &ifr,
			  efi_ifr_string ( &ifr, "Device" ),
			  efi_ifr_string ( &ifr, "Hardware device" ),
			  efi_ifr_string ( &ifr, "%s", dev->name ) );
	efi_snp_hii_questions ( snpdev, &ifr, varstore_id );
	efi_ifr_end_op ( &ifr );
	efi_ifr_end_op ( &ifr );

	/* Build package */
	package = efi_ifr_package ( &ifr, &package_guid, "en-us",
				    efi_ifr_string ( &ifr, "English" ) );
	if ( ! package ) {
		DBGC ( snpdev, "SNPDEV %p could not build IFR package\n",
		       snpdev );
		efi_ifr_free ( &ifr );
		return NULL;
	}

	/* Free temporary storage */
	efi_ifr_free ( &ifr );
	return package;
}
#endif

/**
 * Append response to result string
 *
 * @v snpdev		SNP device
 * @v key		Key
 * @v value		Value
 * @v results		Result string
 * @ret rc		Return status code
 *
 * The result string is allocated dynamically using
 * BootServices::AllocatePool(), and the caller is responsible for
 * eventually calling BootServices::FreePool().
 */
static int efi_snp_hii_append ( struct efi_snp_device *snpdev __unused,
				const char *key, const char *value,
				wchar_t **results ) {
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
	EFI_STATUS efirc;
	size_t len;
	void *new;

	/* Allocate new string */
	len = ( ( *results ? ( wcslen ( *results ) + 1 /* "&" */ ) : 0 ) +
		strlen ( key ) + 1 /* "=" */ + strlen ( value ) + 1 /* NUL */ );
	if ( ( efirc = bs->AllocatePool ( EfiBootServicesData,
					  ( len * sizeof ( wchar_t ) ),
					  &new ) ) != 0 )
		return -EEFI ( efirc );

	/* Populate string */
	efi_snprintf ( new, len, "%ls%s%s=%s", ( *results ? *results : L"" ),
		       ( *results ? L"&" : L"" ), key, value );
	bs->FreePool ( *results );
	*results = new;

	return 0;
}

#ifdef PEN_IONIC_EFIROM
static int ionic_fetch_vlan_mode (struct efi_snp_device *snpdev)
{
    struct net_device *netdev = snpdev->netdev;
    struct ionic *ionic = netdev->priv;
    struct lif *lif = ionic->lif;

    fetch_len = sizeof(UINT8);
    return lif->vlan_en;
}

static int ionic_fetch_vlan_id (struct efi_snp_device *snpdev)
{
    struct net_device *netdev = snpdev->netdev;
    struct ionic *ionic = netdev->priv;
    struct lif *lif = ionic->lif;

    fetch_len = sizeof(UINT16);
    return (int) lif->vlan_id;
}

static int ionic_fetch_bmc_support (struct efi_snp_device *snpdev)
{
    struct net_device *netdev = snpdev->netdev;
    struct ionic *ionic = netdev->priv;
    struct lif *lif = ionic->lif;

    fetch_len = sizeof(UINT8);
    return lif->ncsi_cap;
}

static int ionic_fetch_bmc_interface (struct efi_snp_device *snpdev)
{
    struct net_device *netdev = snpdev->netdev;
    struct ionic *ionic = netdev->priv;
    struct lif *lif = ionic->lif;
    fetch_len = sizeof(UINT8);
    return lif->oob_en;
}

static int ionic_fetch_blink_led (struct efi_snp_device *snpdev)
{
    struct net_device *netdev = snpdev->netdev;
    struct ionic *ionic = netdev->priv;
    struct lif *lif = ionic->lif;

    fetch_len = sizeof(UINT8);
    return lif->uid_led_on;
}

static int ionic_fetch_vis_mode (struct efi_snp_device *snpdev __unused)
{
    fetch_len = sizeof(UINT8);
    return 0;
}

static int ionic_fetch_vis_func (struct efi_snp_device *snpdev __unused)
{
    fetch_len = sizeof(UINT8);
    return 0;
}

void ionic_fetch_sync_up (UINT16 SnpIndex)
{
    struct efi_snp_device *snpdev;
    struct net_device *netdev;
    struct ionic *ionic;
    struct lif *lif;
    UINT8  Data8;
    UINT16  Data16;
    EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
    CHAR16          *String = NULL;
    UINT16          StringId = 0;

    if((SnpIndex & IONIC_NIC_BASE_FORM_ID) == IONIC_NIC_BASE_FORM_ID){
        snpdev = (struct efi_snp_device *)GetMatchedSnpDevForm(SnpIndex);
    }else{
        snpdev = (struct efi_snp_device *)GetMatchedSnpDev(SnpIndex);
    }

    if(snpdev == NULL) return;

    netdev = snpdev->netdev;
    ionic = netdev->priv;
    lif = ionic->lif;

    Data8 = lif->vlan_en;
    HiiSetBrowserData(&NicHii->VlanMode.VarstoreGuid,L"VlanMode",sizeof(UINT8),(UINT8 *)&Data8);

    Data16 = lif->vlan_id;
    HiiSetBrowserData(&NicHii->VlanId.VarstoreGuid,L"VlanId",sizeof(UINT16),(UINT8 *)&Data16);

    Data8 = lif->uid_led_on;
    HiiSetBrowserData(&NicHii->BLed.VarstoreGuid,L"BlinkLed",sizeof(UINT8),(UINT8 *)&Data8);

    Data8 = lif->ncsi_cap;
    HiiSetBrowserData(&NicHii->BmcSupport.VarstoreGuid,L"BmcSupport",sizeof(UINT8),(UINT8 *)&Data8);

    Data8 = lif->oob_en;
    HiiSetBrowserData(&NicHii->BmcInterface.VarstoreGuid,L"BmcInterface",sizeof(UINT8),(UINT8 *)&Data8);

    //Update the latest link status
    if(ionic->lif->info->status.link_status != LastLnkStatus){
        LastLnkStatus = ionic->lif->info->status.link_status;
        bs->AllocatePool(EfiBootServicesData, IONIC_STRING_BUFFER_SIZE, (VOID**)&String);
        bs->SetMem(String,IONIC_STRING_BUFFER_SIZE,0);

        if(LastLnkStatus == IONIC_PORT_OPER_STATUS_UP){
            UnicodeSPrint(String, IONIC_STRING_BUFFER_SIZE, "Link is up");
        }else if(LastLnkStatus == IONIC_PORT_OPER_STATUS_DOWN){
            UnicodeSPrint(String, IONIC_STRING_BUFFER_SIZE, "Link is down");
        }else{
            UnicodeSPrint(String, IONIC_STRING_BUFFER_SIZE, "Link is unknown");
        }

        StringId = NicHii->LinkStatus.Hii.TextTwo;

        HiiSetString(HiiHandle,String,&StringId);
        bs->FreePool( String );

    }

}

struct ionic_fetch_operation {
    /** Setting */
    const char *name;
    int ( * fetch ) (struct efi_snp_device *snpdev);
};

static struct ionic_fetch_operation ionic_fetch_operation[] = {
    { vlan_mode_tse_string, ionic_fetch_vlan_mode },
    { vlan_id_tes_string, ionic_fetch_vlan_id },
    { bmc_support_tes_string, ionic_fetch_bmc_support },
    { bmc_interface_tes_string, ionic_fetch_bmc_interface },
    { blink_led_tes_string, ionic_fetch_blink_led },
    { vis_mode_tes_string, ionic_fetch_vis_mode },
    { vis_func_tes_string, ionic_fetch_vis_func },
};

static int ionic_fetch ( struct efi_snp_device *snpdev, const char *name ) {

    struct ionic_fetch_operation *op;
    unsigned int i;
    uint8_t len;

    for ( i = 0 ; i < ( sizeof ( ionic_fetch_operation ) /sizeof ( ionic_fetch_operation[0] ) ) ; i++ )
    {
        len = strlen(name);
        op = &ionic_fetch_operation[i];
        if ( ! memcmp ( op->name, name, len ) ) {
            fetch_number = op->fetch (snpdev);
            return 0;
        }
    }
    return -1;
}

/**
 * Fetch HII setting
 *
 * @v snpdev		SNP device
 * @v key		Key
 * @v value		Value
 * @v results		Result string
 * @v have_setting	Flag indicating detection of a setting
 * @ret rc		Return status code
 */
static int efi_snp_hii_fetch_ionic ( struct efi_snp_device *snpdev,
                   const char *key, const char *value,
                   wchar_t **results, int *have_setting ) {
    int rc;
    CHAR8 ValueKey[] = "VALUE";
    char buf[5];

    /* Handle ConfigHdr components */
    if ( ( strcasecmp ( key, "GUID" ) == 0 ) ||
         ( strcasecmp ( key, "NAME" ) == 0 ) ||
         ( strcasecmp ( key, "PATH" ) == 0 ) ||
         ( strcasecmp ( key, "OFFSET" ) == 0 ) ||
         ( strcasecmp ( key, "WIDTH" ) == 0 )
    ) {
        rc = efi_snp_hii_append ( snpdev, key, value, results );
        if( rc != 0 ) return rc;
    }

    if( strcasecmp ( key, "NAME" ) == 0 ){
        rc = ionic_fetch(snpdev ,value);
        if( rc != 0 ) return rc;
    }

    if( strcasecmp ( key, "WIDTH" ) == 0 ){
        if(fetch_len == sizeof(UINT8)){
            snprintf( buf, sizeof(buf), "%02X", fetch_number);
        }else if(fetch_len == sizeof(UINT16)){
            snprintf( buf, sizeof(buf), "%04X", fetch_number);
        }
        rc = efi_snp_hii_append ( snpdev, ValueKey, buf, results );
        if ( have_setting ) *have_setting = 1;
    }
    return rc;
}
#endif

/**
 * Fetch HII setting
 *
 * @v snpdev		SNP device
 * @v key		Key
 * @v value		Value
 * @v results		Result string
 * @v have_setting	Flag indicating detection of a setting
 * @ret rc		Return status code
 */
static int efi_snp_hii_fetch ( struct efi_snp_device *snpdev,
			       const char *key, const char *value,
			       wchar_t **results, int *have_setting ) {
	struct settings *settings = efi_snp_hii_settings ( snpdev );
	struct settings *origin;
	struct setting *setting;
	struct setting fetched;
	int len;
	char *buf;
	char *encoded;
	int i;
	int rc;

	/* Handle ConfigHdr components */
	if ( ( strcasecmp ( key, "GUID" ) == 0 ) ||
	     ( strcasecmp ( key, "NAME" ) == 0 ) ||
	     ( strcasecmp ( key, "PATH" ) == 0 ) ) {
		return efi_snp_hii_append ( snpdev, key, value, results );
	}
	if ( have_setting )
		*have_setting = 1;

	/* Do nothing more unless we have a settings block */
	if ( ! settings ) {
		rc = -ENOTSUP;
		goto err_no_settings;
	}

	/* Identify setting */
	setting = find_setting ( key );
	if ( ! setting ) {
		DBGC ( snpdev, "SNPDEV %p no such setting \"%s\"\n",
		       snpdev, key );
		rc = -ENODEV;
		goto err_find_setting;
	}

	/* Encode value */
	if ( setting_exists ( settings, setting ) ) {

		/* Calculate formatted length */
		len = fetchf_setting ( settings, setting, &origin, &fetched,
				       NULL, 0 );
		if ( len < 0 ) {
			rc = len;
			DBGC ( snpdev, "SNPDEV %p could not fetch %s: %s\n",
			       snpdev, setting->name, strerror ( rc ) );
			goto err_fetchf_len;
		}

		/* Allocate buffer for formatted value and HII-encoded value */
		buf = zalloc ( len + 1 /* NUL */ + ( len * 4 ) + 1 /* NUL */ );
		if ( ! buf ) {
			rc = -ENOMEM;
			goto err_alloc;
		}
		encoded = ( buf + len + 1 /* NUL */ );

		/* Format value */
		fetchf_setting ( origin, &fetched, NULL, NULL, buf,
				 ( len + 1 /* NUL */ ) );
		for ( i = 0 ; i < len ; i++ ) {
			sprintf ( ( encoded + ( 4 * i ) ), "%04x",
				  *( ( uint8_t * ) buf + i ) );
		}

	} else {

		/* Non-existent or inapplicable setting */
		buf = NULL;
		encoded = "";
	}

	/* Append results */
	if ( ( rc = efi_snp_hii_append ( snpdev, key, encoded,
					 results ) ) != 0 ) {
		goto err_append;
	}

	/* Success */
	rc = 0;

 err_append:
	free ( buf );
 err_alloc:
 err_fetchf_len:
 err_find_setting:
 err_no_settings:
	return rc;
}

#ifndef PEN_IONIC_EFIROM
/**
 * Fetch HII setting
 *
 * @v snpdev		SNP device
 * @v key		Key
 * @v value		Value
 * @v results		Result string (unused)
 * @v have_setting	Flag indicating detection of a setting (unused)
 * @ret rc		Return status code
 */
static int efi_snp_hii_store ( struct efi_snp_device *snpdev,
			       const char *key, const char *value,
			       wchar_t **results __unused,
			       int *have_setting __unused ) {
	struct settings *settings = efi_snp_hii_settings ( snpdev );
	struct setting *setting;
	char *buf;
	char tmp[5];
	char *endp;
	int len;
	int i;
	int rc;

	/* Handle ConfigHdr components */
	if ( ( strcasecmp ( key, "GUID" ) == 0 ) ||
	     ( strcasecmp ( key, "NAME" ) == 0 ) ||
	     ( strcasecmp ( key, "PATH" ) == 0 ) ) {
		/* Nothing to do */
		return 0;
	}

	/* Do nothing more unless we have a settings block */
	if ( ! settings ) {
		rc = -ENOTSUP;
		goto err_no_settings;
	}

	/* Identify setting */
	setting = find_setting ( key );
	if ( ! setting ) {
		DBGC ( snpdev, "SNPDEV %p no such setting \"%s\"\n",
		       snpdev, key );
		rc = -ENODEV;
		goto err_find_setting;
	}

	/* Allocate buffer */
	len = ( strlen ( value ) / 4 );
	buf = zalloc ( len + 1 /* NUL */ );
	if ( ! buf ) {
		rc = -ENOMEM;
		goto err_alloc;
	}

	/* Decode value */
	tmp[4] = '\0';
	for ( i = 0 ; i < len ; i++ ) {
		memcpy ( tmp, ( value + ( i * 4 ) ), 4 );
		buf[i] = strtoul ( tmp, &endp, 16 );
		if ( endp != &tmp[4] ) {
			DBGC ( snpdev, "SNPDEV %p invalid character %s\n",
			       snpdev, tmp );
			rc = -EINVAL;
			goto err_inval;
		}
	}

	/* Store value */
	if ( ( rc = storef_setting ( settings, setting, buf ) ) != 0 ) {
		DBGC ( snpdev, "SNPDEV %p could not store \"%s\" into %s: %s\n",
		       snpdev, buf, setting->name, strerror ( rc ) );
		goto err_storef;
	}

	/* Success */
	rc = 0;

 err_storef:
 err_inval:
	free ( buf );
 err_alloc:
 err_find_setting:
 err_no_settings:
	return rc;
}
#endif

/**
 * Process portion of HII configuration string
 *
 * @v snpdev		SNP device
 * @v string		HII configuration string
 * @v progress		Progress through HII configuration string
 * @v results		Results string
 * @v have_setting	Flag indicating detection of a setting (unused)
 * @v process		Function used to process key=value pairs
 * @ret rc		Return status code
 */
static int efi_snp_hii_process ( struct efi_snp_device *snpdev,
				 wchar_t *string, wchar_t **progress,
				 wchar_t **results, int *have_setting,
				 int ( * process ) ( struct efi_snp_device *,
						     const char *key,
						     const char *value,
						     wchar_t **results,
						     int *have_setting ) ) {
	wchar_t *wkey = string;
	wchar_t *wend = string;
	wchar_t *wvalue = NULL;
	size_t key_len;
	size_t value_len;
	void *temp;
	char *key;
	char *value;
	int rc;

	/* Locate key, value (if any), and end */
	while ( *wend ) {
		if ( *wend == L'&' )
			break;
		if ( *(wend++) == L'=' )
			wvalue = wend;
	}

	/* Allocate memory for key and value */
	key_len = ( ( wvalue ? ( wvalue - 1 ) : wend ) - wkey );
	value_len = ( wvalue ? ( wend - wvalue ) : 0 );
	temp = zalloc ( key_len + 1 /* NUL */ + value_len + 1 /* NUL */ );
	if ( ! temp )
		return -ENOMEM;
	key = temp;
	value = ( temp + key_len + 1 /* NUL */ );

	/* Copy key and value */
	while ( key_len-- )
		key[key_len] = wkey[key_len];
	while ( value_len-- )
		value[value_len] = wvalue[value_len];

	/* Process key and value */
	if ( ( rc = process ( snpdev, key, value, results,
			      have_setting ) ) != 0 ) {
		goto err;
	}

	/* Update progress marker */
	*progress = wend;

 err:
	/* Free temporary storage */
	free ( temp );

	return rc;
}

/**
 * Fetch configuration
 *
 * @v hii		HII configuration access protocol
 * @v request		Configuration to fetch
 * @ret progress	Progress made through configuration to fetch
 * @ret results		Query results
 * @ret efirc		EFI status code
 */
static EFI_STATUS EFIAPI
efi_snp_hii_extract_config ( const EFI_HII_CONFIG_ACCESS_PROTOCOL *hii,
			     EFI_STRING request, EFI_STRING *progress,
			     EFI_STRING *results ) {
	struct efi_snp_device *snpdev =
		container_of ( hii, struct efi_snp_device, hii );
	int have_setting = 0;
	wchar_t *pos;
	int rc;

	DBGC ( snpdev, "SNPDEV %p ExtractConfig request \"%ls\"\n",
	       snpdev, request );

	/* Initialise results */
	*results = NULL;

	/* Work around apparently broken UEFI specification */
	if ( ! ( request && request[0] ) ) {
		DBGC ( snpdev, "SNPDEV %p ExtractConfig ignoring malformed "
		       "request\n", snpdev );
		return EFI_INVALID_PARAMETER;
	}

	/* Process all request fragments */
	for ( pos = *progress = request ; *progress && **progress ;
	      pos = *progress + 1 ) {
		if ( ( rc = efi_snp_hii_process ( snpdev, pos, progress,
						  results, &have_setting,
#ifdef PEN_IONIC_EFIROM
						  efi_snp_hii_fetch_ionic
#else
						  efi_snp_hii_fetch
#endif
						  ) ) != 0 ) {
			return EFIRC ( rc );
		}
	}

	/* If we have no explicit request, return all settings */
	if ( ! have_setting ) {
		struct setting *setting;

		for_each_table_entry ( setting, SETTINGS ) {
			if ( ! efi_snp_hii_setting_applies ( snpdev, setting ) )
				continue;
			if ( ( rc = efi_snp_hii_fetch ( snpdev, setting->name,
							NULL, results,
							NULL ) ) != 0 ) {
				return EFIRC ( rc );
			}
		}
	}

	DBGC ( snpdev, "SNPDEV %p ExtractConfig results \"%ls\"\n",
	       snpdev, *results );
	return 0;
}

/**
 * Store configuration
 *
 * @v hii		HII configuration access protocol
 * @v config		Configuration to store
 * @ret progress	Progress made through configuration to store
 * @ret efirc		EFI status code
 */
#ifdef PEN_IONIC_EFIROM
static EFI_STATUS EFIAPI
efi_snp_hii_route_config ( const EFI_HII_CONFIG_ACCESS_PROTOCOL *hii __unused,
			   EFI_STRING config __unused, EFI_STRING *progress ) {
	if (progress == NULL) {
		return EFI_INVALID_PARAMETER;
	}

	*progress = NULL;
	return EFI_SUCCESS;
}
#else
static EFI_STATUS EFIAPI
efi_snp_hii_route_config ( const EFI_HII_CONFIG_ACCESS_PROTOCOL *hii,
			   EFI_STRING config, EFI_STRING *progress ) {
	struct efi_snp_device *snpdev =
		container_of ( hii, struct efi_snp_device, hii );
	wchar_t *pos;
	int rc;

	DBGC ( snpdev, "SNPDEV %p RouteConfig \"%ls\"\n", snpdev, config );

	/* Process all request fragments */
	for ( pos = *progress = config ; *progress && **progress ;
	      pos = *progress + 1 ) {
		if ( ( rc = efi_snp_hii_process ( snpdev, pos, progress,
						  NULL, NULL,
						  efi_snp_hii_store ) ) != 0 ) {
			return EFIRC ( rc );
		}
	}

	return 0;
}
#endif

/**
 * Handle form actions
 *
 * @v hii		HII configuration access protocol
 * @v action		Form browser action
 * @v question_id	Question ID
 * @v type		Type of value
 * @v value		Value
 * @ret action_request	Action requested by driver
 * @ret efirc		EFI status code
 */
#ifdef PEN_IONIC_EFIROM
static EFI_STATUS EFIAPI
efi_snp_hii_callback ( const EFI_HII_CONFIG_ACCESS_PROTOCOL *hii __unused,
               EFI_BROWSER_ACTION action,
               EFI_QUESTION_ID question_id,
               UINT8 type __unused,
               EFI_IFR_TYPE_VALUE *value,
               EFI_BROWSER_ACTION_REQUEST *action_request ) {

    EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
    EFI_STRING      String = NULL;
    CHAR8           *Buffer = NULL;
    UINT8           BufferLen = 0;
    UINT16          Id = 0;
    UINT16          Index;

    Index = question_id & 0xFF00;
    if ((action == EFI_BROWSER_ACTION_FORM_OPEN) || (action == EFI_BROWSER_ACTION_FORM_CLOSE)){
        if (action == EFI_BROWSER_ACTION_FORM_OPEN) {
            ionic_fetch_sync_up(Index);
        }
        return EFI_SUCCESS;
    }

    if ((action == EFI_BROWSER_ACTION_CHANGING) || (action == EFI_BROWSER_ACTION_SUBMITTED)){
        return EFI_SUCCESS;
    }

    if ((action != EFI_BROWSER_ACTION_CHANGED) && (action != EFI_BROWSER_ACTION_DEFAULT_STANDARD)){
        return EFI_UNSUPPORTED;
    }

    if((value == NULL) || (action_request == NULL)) {
        return EFI_INVALID_PARAMETER;
    }

    Id = question_id & 0xFF;

    if((Id < IONIC_VLAN_MODE_QUESTION) || (Id > IONIC_BMC_SUPPORT_QUESTION)) {
        return EFI_UNSUPPORTED;
    }

    if((Id >= IONIC_VLAN_MODE_QUESTION) && (Id <= IONIC_BMC_SUPPORT_QUESTION)) {
        if(action == EFI_BROWSER_ACTION_DEFAULT_STANDARD) {
            if(Nic->LoadDefaultDone == 0){
                if(Nic->LoadDefault)
                    Nic->LoadDefault(Index,&Buffer,&BufferLen);
                Nic->LoadDefaultDone = 1;
                ionic_fetch_sync_up(Index);
            }
            return EFI_SUCCESS;
        }
    }

    Nic->LoadDefaultDone = 0;
    switch(Id){
        case IONIC_VLAN_MODE_QUESTION:
            Nic->VlanModeVar = value->b;
            if(NicHii->VlanMode.Callback) NicHii->VlanMode.Callback(Index,&Buffer,&BufferLen);
            if(Nic->CallbackResult != IONIC_RC_SUCCESS){
                HiiCreatePopUp(EfiHiiPopupStyleWarning,EfiHiiPopupTypeOk,HiiHandle,NicHii->VlanMode.Hii.Warning);
                ionic_fetch_sync_up(Index);
            }
            break;
        case IONIC_VLAN_ID_QUESTION:
            Nic->VlanIdVar = value->u16_1;

            if(NicHii->VlanId.Callback) NicHii->VlanId.Callback(Index,&Buffer,&BufferLen);
            if(Nic->CallbackResult != IONIC_RC_SUCCESS){
                HiiCreatePopUp(EfiHiiPopupStyleWarning,EfiHiiPopupTypeOk,HiiHandle,NicHii->VlanId.Hii.Warning);
                ionic_fetch_sync_up(Index);
            }
            break;
        case IONIC_VIRTUAL_MODE_QUESTION:
            Nic->VirtualModeVar = value->b;
            if(NicHii->VirtualMode.Callback) NicHii->VirtualMode.Callback(Index,&Buffer,&BufferLen);
            if(Nic->CallbackResult != IONIC_RC_SUCCESS){
                HiiCreatePopUp(EfiHiiPopupStyleWarning,EfiHiiPopupTypeOk,HiiHandle,NicHii->VirtualMode.Hii.Warning);
                ionic_fetch_sync_up(Index);
            }
            break;
        case IONIC_VIRTUAL_FUNC_QUESTION:
            Nic->VirtualFuncVar = value->b;
            if(NicHii->VirtualFunc.Callback) NicHii->VirtualFunc.Callback(Index,&Buffer,&BufferLen);
            if(Nic->CallbackResult != IONIC_RC_SUCCESS){
                HiiCreatePopUp(EfiHiiPopupStyleWarning,EfiHiiPopupTypeOk,HiiHandle,NicHii->VirtualFunc.Hii.Warning);
                ionic_fetch_sync_up(Index);
            }
            break;
        case IONIC_BMC_INTERFACE_QUESTION:
            Nic->BmcInterfaceVar = value->b;
            if(NicHii->BmcInterface.Callback) NicHii->BmcInterface.Callback(Index,&Buffer,&BufferLen);
            if(Nic->CallbackResult != IONIC_RC_SUCCESS){
                HiiCreatePopUp(EfiHiiPopupStyleWarning,EfiHiiPopupTypeOk,HiiHandle,NicHii->BmcInterface.Hii.Warning);
                ionic_fetch_sync_up(Index);
            }
            break;
        case IONIC_BLINK_LED_QUESTION:
            Nic->BlinkLedVar = value->b;
            if(NicHii->BLed.Callback) NicHii->BLed.Callback(Index,&Buffer,&BufferLen);
            if(Nic->CallbackResult != IONIC_RC_SUCCESS){
                HiiCreatePopUp(EfiHiiPopupStyleWarning,EfiHiiPopupTypeOk,HiiHandle,NicHii->BLed.Hii.Warning);
                ionic_fetch_sync_up(Index);
            }
            break;
        case IONIC_MAC_ADDR_QUESTION:
            String = HiiGetString(HiiHandle,value->string,&BufferLen,"English");
            if(String){
                bs->SetMem((UINT8 *)&Nic->NicMacVar[0],sizeof(Nic->NicMacVar),0);
                bs->CopyMem((UINT8 *)&(Nic->NicMacVar[0]),(UINT8 *)String,BufferLen);
                Buffer = (CHAR8 *)String;
                if(NicHii->MacAddr.Callback) NicHii->MacAddr.Callback(Index,&Buffer,&BufferLen);
            }
            break;
        case IONIC_VIRTUAL_MAC_QUESTION:
            String = HiiGetString(HiiHandle,value->string,&BufferLen,"English");
            if(String){
                bs->SetMem((UINT8 *)&Nic->NicVirMacVar[0],sizeof(Nic->NicVirMacVar),0);
                bs->CopyMem((UINT8 *)&(Nic->NicVirMacVar[0]),(UINT8 *)String,BufferLen);
                Buffer = (CHAR8 *) String;
                if(NicHii->VirtualMacAddr.Callback) NicHii->VirtualMacAddr.Callback(Index,&Buffer,&BufferLen);
            }
            break;
    }
    return EFI_SUCCESS;
}

#else

static EFI_STATUS EFIAPI
efi_snp_hii_callback ( const EFI_HII_CONFIG_ACCESS_PROTOCOL *hii,
		       EFI_BROWSER_ACTION action __unused,
		       EFI_QUESTION_ID question_id __unused,
		       UINT8 type __unused, EFI_IFR_TYPE_VALUE *value __unused,
		       EFI_BROWSER_ACTION_REQUEST *action_request __unused ) {
	struct efi_snp_device *snpdev =
		container_of ( hii, struct efi_snp_device, hii );

	DBGC ( snpdev, "SNPDEV %p Callback\n", snpdev );
	return EFI_UNSUPPORTED;
}
#endif

/** HII configuration access protocol */
static EFI_HII_CONFIG_ACCESS_PROTOCOL efi_snp_device_hii = {
	.ExtractConfig	= efi_snp_hii_extract_config,
	.RouteConfig	= efi_snp_hii_route_config,
	.Callback	= efi_snp_hii_callback,
};

/**
 * Install HII protocol and packages for SNP device
 *
 * @v snpdev		SNP device
 * @ret rc		Return status code
 */
int efi_snp_hii_install ( struct efi_snp_device *snpdev ) {
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
	VENDOR_DEVICE_PATH *vendor_path;
	EFI_DEVICE_PATH_PROTOCOL *path_end;
	size_t path_prefix_len;
	int leak = 0;
	EFI_STATUS efirc;
	int rc;

	/* Do nothing if HII database protocol is not supported */
	if ( ! efihii ) {
		rc = -ENOTSUP;
		goto err_no_hii;
	}

	/* Initialise HII protocol */
	memcpy ( &snpdev->hii, &efi_snp_device_hii, sizeof ( snpdev->hii ) );

	/* Create HII package list */
#ifdef PEN_IONIC_EFIROM
	snpdev->package_list = efi_snp_hii_package_list_ionic ( snpdev );
#else
	snpdev->package_list = efi_snp_hii_package_list ( snpdev );
#endif
	if ( ! snpdev->package_list ) {
		DBGC ( snpdev, "SNPDEV %p could not create HII package list\n",
		       snpdev );
		rc = -ENOMEM;
		goto err_build_package_list;
	}

	/* Allocate the new device path */
	path_prefix_len = efi_path_len ( snpdev->path );
	snpdev->hii_child_path = zalloc ( path_prefix_len +
					  sizeof ( *vendor_path ) +
					  sizeof ( *path_end ) );
	if ( ! snpdev->hii_child_path ) {
		DBGC ( snpdev,
		       "SNPDEV %p could not allocate HII child device path\n",
		       snpdev );
		rc = -ENOMEM;
		goto err_alloc_child_path;
	}

	/* Populate the device path */
	memcpy ( snpdev->hii_child_path, snpdev->path, path_prefix_len );
	vendor_path = ( ( ( void * ) snpdev->hii_child_path ) +
			path_prefix_len );
	vendor_path->Header.Type = HARDWARE_DEVICE_PATH;
	vendor_path->Header.SubType = HW_VENDOR_DP;
	vendor_path->Header.Length[0] = sizeof ( *vendor_path );
	efi_snp_hii_random_guid ( &vendor_path->Guid );
	path_end = ( ( void * ) ( vendor_path + 1 ) );
	path_end->Type = END_DEVICE_PATH_TYPE;
	path_end->SubType = END_ENTIRE_DEVICE_PATH_SUBTYPE;
	path_end->Length[0] = sizeof ( *path_end );

	/* Create device path and child handle for HII association */
	/* Install HII protocol */
	if ( ( efirc = bs->InstallMultipleProtocolInterfaces (
			&snpdev->hii_child_handle,
			&efi_device_path_protocol_guid, snpdev->hii_child_path,
#ifdef PEN_IONIC_EFIROM
			&efi_hii_config_access_protocol_guid, &snpdev->hii,
#endif
			NULL ) ) != 0 ) {
		rc = -EEFI ( efirc );
		DBGC ( snpdev, "SNPDEV %p could not create HII child handle: "
		       "%s\n", snpdev, strerror ( rc ) );
		goto err_hii_child_handle;
	}

	/* Add HII packages */
	if ( ( efirc = efihii->NewPackageList ( efihii, snpdev->package_list,
						snpdev->hii_child_handle,
						&snpdev->hii_handle ) ) != 0 ) {
		rc = -EEFI ( efirc );
		DBGC ( snpdev, "SNPDEV %p could not add HII packages: %s\n",
		       snpdev, strerror ( rc ) );
		goto err_new_package_list;
	}

#ifndef PEN_IONIC_EFIROM
	/* Install HII protocol */
	if ( ( efirc = bs->InstallMultipleProtocolInterfaces (
			 &snpdev->hii_child_handle,
			 &efi_hii_config_access_protocol_guid, &snpdev->hii,
			 NULL ) ) != 0 ) {
		rc = -EEFI ( efirc );
		DBGC ( snpdev, "SNPDEV %p could not install HII protocol: %s\n",
		       snpdev, strerror ( rc ) );
		goto err_install_protocol;
	}
#endif
#ifdef PEN_IONIC_EFIROM
	HiiHandle = snpdev->hii_handle;
#endif

	/* Add as child of handle with SNP instance */
	if ( ( rc = efi_child_add ( snpdev->handle,
				    snpdev->hii_child_handle ) ) != 0 ) {
		DBGC ( snpdev,
		       "SNPDEV %p could not adopt HII child handle: %s\n",
		       snpdev, strerror ( rc ) );
		goto err_efi_child_add;
	}

	return 0;

	efi_child_del ( snpdev->handle, snpdev->hii_child_handle );
 err_efi_child_add:
	if ( ( efirc = bs->UninstallMultipleProtocolInterfaces (
			snpdev->hii_child_handle,
			&efi_hii_config_access_protocol_guid, &snpdev->hii,
			NULL ) ) != 0 ) {
		DBGC ( snpdev, "SNPDEV %p could not uninstall HII protocol: "
		       "%s\n", snpdev, strerror ( -EEFI ( efirc ) ) );
		leak = 1;
	}
	efi_nullify_hii ( &snpdev->hii );
#ifndef PEN_IONIC_EFIROM
 err_install_protocol:
#endif
	if ( ! leak )
		efihii->RemovePackageList ( efihii, snpdev->hii_handle );
 err_new_package_list:
	if ( ( efirc = bs->UninstallMultipleProtocolInterfaces (
			snpdev->hii_child_handle,
			&efi_device_path_protocol_guid, snpdev->hii_child_path,
			NULL ) ) != 0 ) {
		DBGC ( snpdev, "SNPDEV %p could not uninstall HII path: %s\n",
		       snpdev, strerror ( -EEFI ( efirc ) ) );
		leak = 1;
	}
 err_hii_child_handle:
	if ( ! leak ) {
		free ( snpdev->hii_child_path );
		snpdev->hii_child_path = NULL;
	}
 err_alloc_child_path:
	if ( ! leak ) {
		free ( snpdev->package_list );
		snpdev->package_list = NULL;
	}
 err_build_package_list:
 err_no_hii:
	return rc;
}

/**
 * Uninstall HII protocol and package for SNP device
 *
 * @v snpdev		SNP device
 * @ret leak		Uninstallation failed: leak memory
 */
int efi_snp_hii_uninstall ( struct efi_snp_device *snpdev ) {
	EFI_BOOT_SERVICES *bs = efi_systab->BootServices;
	int leak = efi_shutdown_in_progress;
	EFI_STATUS efirc;

	/* Do nothing if HII database protocol is not supported */
	if ( ! efihii )
		return 0;

	/* Uninstall protocols and remove package list */
	efi_child_del ( snpdev->handle, snpdev->hii_child_handle );
	if ( ( ! efi_shutdown_in_progress ) &&
	     ( ( efirc = bs->UninstallMultipleProtocolInterfaces (
			snpdev->hii_child_handle,
			&efi_hii_config_access_protocol_guid, &snpdev->hii,
			NULL ) ) != 0 ) ) {
		DBGC ( snpdev, "SNPDEV %p could not uninstall HII protocol: "
		       "%s\n", snpdev, strerror ( -EEFI ( efirc ) ) );
		leak = 1;
	}
	efi_nullify_hii ( &snpdev->hii );
	if ( ! leak )
		efihii->RemovePackageList ( efihii, snpdev->hii_handle );
	if ( ( ! efi_shutdown_in_progress ) &&
	     ( ( efirc = bs->UninstallMultipleProtocolInterfaces (
			snpdev->hii_child_handle,
			&efi_device_path_protocol_guid, snpdev->hii_child_path,
			NULL ) ) != 0 ) ) {
		DBGC ( snpdev, "SNPDEV %p could not uninstall HII path: %s\n",
		       snpdev, strerror ( -EEFI ( efirc ) ) );
		leak = 1;
	}
	if ( ! leak ) {
		free ( snpdev->hii_child_path );
		snpdev->hii_child_path = NULL;
		free ( snpdev->package_list );
		snpdev->package_list = NULL;
	}

#ifdef PEN_IONIC_EFIROM
	uninstall_ionic_hii();
#endif

	/* Report leakage, if applicable */
	if ( leak && ( ! efi_shutdown_in_progress ) )
		DBGC ( snpdev, "SNPDEV %p HII nullified and leaked\n", snpdev );
	return leak;
}
