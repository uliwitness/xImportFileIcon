// -----------------------------------------------------------------------------
//	Headers
// -----------------------------------------------------------------------------

#include <HyperXCmd.h>
#include <Types.h>
#include <Memory.h>
#include <OSUtils.h>
#include <ToolUtils.h>
#include <Files.h>
#include <Aliases.h>
#include <Errors.h>
#include <Resources.h>
#include <string.h>
#include "XCmdUtils.h"


#define MIN(a,b) (((a) < (b)) ? (a) : (b))

OSErr GetFileIconForType(short drvNum, OSType type, OSType creator, SInt8 iconType, char* outBuffer, long bufferSize);


// -----------------------------------------------------------------------------
//	GetFileIconForType
// -----------------------------------------------------------------------------

OSErr GetFileIconForType(short drvNum, OSType type, OSType creator, SInt8 iconType, char* outBuffer, long bufferSize) {
	OSErr 		err = noErr;
	short 		dirID = 0;
	DTPBRec 	paramBlock = {0};
	char 		buffer[kLargeIconSize] = {0};
	short 		iconID = 0;
	Handle 		iconData = NULL;
	Str255		volName = {0};
	long		freeBytes = 0;
	short		bootVRefNum = 0;
	HParamBlockRec	vInfoPB = {0};
	
	memset(&paramBlock, 0, sizeof(paramBlock));
	memset(&vInfoPB, 0, sizeof(vInfoPB));
	
	// Find volume ref num:
	vInfoPB.volumeParam.ioVolIndex = drvNum;
	err = PBHGetVInfo(&vInfoPB, false);
	if (err == paramErr) {
		return afpItemNotFound;
	} else if (err != noErr) {
		return err;
	}
	bootVRefNum = vInfoPB.volumeParam.ioVRefNum;
	
	// Open Desktop database on that volume:
	paramBlock.ioVRefNum = bootVRefNum;
	err = PBDTGetPath(&paramBlock);
	if (err != noErr) {
		return afpItemNotFound;
	} else if (err != noErr) {
		return err;
	} else if (paramBlock.ioDTRefNum == 0) {
		return afpItemNotFound;
	}
	
	// Actually grab the icon data (includes mask for B/W icons):
	paramBlock.ioVRefNum = 0;
	paramBlock.ioDTBuffer = outBuffer;
	paramBlock.ioDTReqCount = bufferSize;
	paramBlock.ioIconType = iconType;
	paramBlock.ioFileCreator = creator;
	paramBlock.ioFileType = type;
	err = PBDTGetIconSync(&paramBlock);
	if (err != noErr) {
		return err;
	}
	if (paramBlock.ioDTActCount != bufferSize) {
		return resNotFound;
	}
	
	return noErr;
}


// -----------------------------------------------------------------------------
//	xcmdmain
// -----------------------------------------------------------------------------

void xcmdmain(void)
{
	Str255 		errStr = {0};
	OSErr 		err = noErr;
	short 		dirID = 0;
	Str255 		typeCreator;
	char 		buffer[kLargeIconSize] = {0};
	OSType		type;
	OSType		creator;
	short 		iconID = 0;
	Handle 		iconData = NULL;
	Handle		existingIcon;
	short 		drvNum = 0;
	SInt8		iconSize = kLargeIcon;
	long		iconBufferSize = kLargeIconSize;
	Str255		iconSizeStr = {0};
	Str255		iconName = {0};
		
	if (!GetIndXParameter255(1, typeCreator)) {
		AppendReturnValue("\pExpected file type and creator as first parameter.");
		return;
	}
	
	if (GetIndXParameter255(2, iconSizeStr)) {
		if (EqualString("\plarge", iconSizeStr, false, true)) {
			iconSize = kLargeIcon; 
			iconBufferSize = kLargeIconSize;
		} else if (EqualString("\psmall", iconSizeStr, false, true)) {
			iconSize = kSmallIcon; 
			iconBufferSize = kSmallIconSize;
		} else if (iconSizeStr[0] != 0) {
			AppendReturnValue("\pSecond parameter should be \"small\" or \"large\".");
			return;
		}
	}

	if (iconBufferSize > sizeof(buffer)) {
		AppendReturnValue("\pINTERNAL ERROR BUFFER SMALLER THAN ICON.");
		return;
	}

	if (EqualString("\p?", typeCreator, false, true)) {
		AppendReturnValue("\pSyntax: xImportFileIcon \"typeCREA\"[, {small|large}]");
		return;
	} else if (EqualString("\p!", typeCreator, false, true)) {
		AppendReturnValue("\p(c) Copyright 2021 by Uli Kusterer, all rights reserved.");
		return;
	} else if (typeCreator[0] != 8) {
		AppendReturnValue("\pType and creator in first parameter must each be 4 characters with no characters in between.");
		return;
	}
	
	if (iconSize == kSmallIcon) {
		CopyCToPString("sm_", iconName);
		AppendString(iconName, typeCreator);
	} else {
		BlockMove(typeCreator, iconName, sizeof(iconName));
	}
	
	BlockMove( typeCreator + 5, &creator, 4);
	BlockMove( typeCreator + 1, &type, 4);
	
	for (drvNum = 1; drvNum < 10000; ++drvNum) {
		err = GetFileIconForType(drvNum, type, creator, iconSize, buffer, iconBufferSize);
		if (err != afpItemNotFound) {
			break;
		}
	}
	if (err != noErr) {
		SetReturnValue("\pError retrieving icon from desktop database ");
		NumToString(drvNum, errStr);
		AppendReturnValue(errStr);
		AppendReturnValue("\p: ");
		NumToString(err, errStr);
		AppendReturnValue(errStr);
		return;
	}
	
	existingIcon = GetNamedResource('ICON', iconName);
	err = ResError();
	if (err != noErr && err != resNotFound) {
		NumToString(err, errStr);
		SetReturnValue("\pError retrieving icon resource: ");
		AppendReturnValue(errStr);
		return;
	}
	if (existingIcon) {
		ResType dummyType;
		Str255 dummyName;
		
		GetResInfo(existingIcon, &iconID, &dummyType, dummyName);
		err = ResError();
		if (err != noErr) {
			NumToString(err, errStr);
			SetReturnValue("\pError determining existing icon ID: ");
			AppendReturnValue(errStr);
			return;
		}
	} else {
		iconID = Unique1ID('ICON');
		iconData = NewHandleClear(128);
		if (iconSize == kLargeIcon) {
			BlockMove(buffer, *iconData, 128);
		} else {
			int 			x = 0;
			int				srcIdx = 0;
			unsigned char*	dest = (unsigned char*) *iconData;
			dest += 4 * 8; // start 8 pixels down.
			for(x = 0; x < 16; ++x) {
				dest += 1; // Skip 8 pixels on left. 
				*(dest++) = buffer[srcIdx++];
				*(dest++) = buffer[srcIdx++];
				dest += 1; // Skip 8 pixels on right. 
			}
		}
		AddResource(iconData, 'ICON', iconID, iconName);
		err = ResError();
		if (err != noErr) {
			DisposeHandle(iconData);
			NumToString(err, errStr);
			SetReturnValue("\pError creating icon: ");
			AppendReturnValue(errStr);
			return;
		}
	}
	NumToString(iconID, errStr);
	SetReturnValue(errStr);
}