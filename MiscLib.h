/********************************************
; File:	MiscLib.h
;
; By:	 Josef W. Wankerl
;
; Copyright EGO Systems 1992-1994
; All Rights Reserved
;
********************************************/

#ifndef __TYPES__
#include <TYPES.h>
#endif

#ifndef __LIST__
#include <LIST.h>
#endif

#ifndef __FINDER__
#include <FINDER.h>
#endif

#ifndef __GSOS__
#include <GSOS.h>
#endif

#ifndef __STDFILE__
#include <STDFILE.h>
#endif

#ifndef __MISCLIB__
#define __MISCLIB__

/* Message center constants: */

#define OpenFlag			0x0000
#define PrintFlag			0x0001
#define NoMessageFlag	0xFFFF

/* TextEdit undo/redo constants: */

#define teeUndo			0x0001
#define teeSmartQuotes	0x0002
#define teeExtendedKeys	0x0004

#define NoUndo				0x0000
#define TypingUndo		0x0001
#define GenericUndo		0x0002
#define CutUndo			0x0003
#define PasteUndo			0x0004
#define ClearUndo			0x0005
#define DeleteLeftUndo	0x0006
#define DeleteRightUndo	0x0007
#define DeleteToEndUndo	0x0008

/* String constants: */

#define stPString			0x0000
#define stCString			0x0001
#define stInputString	0x0002
#define stOutputString	0x0003
#define stTextBlock		0x0004

#define stPointer			0x0000
#define stHandle			0x4000
#define stResource		0x8000
#define stDisposeHandle	0xC000

#define stCaseCompare	0x0000
#define stNoCaseCompare	0x0001

/* namesHandle traversal constants: */

#define trSkipDirectory	0x0000
#define trContentsLast	0x0001
#define trContentsFirst 0x0002
#define trNoContent		0x0003

#define trSkipOnBad		0x0004

#define trContinue		0x0000
#define trSkipFile		0x0001
#define trQuitTraversing 0x0002

/* Delete constants: */

#define delLockPrompts	0x0000
#define delAllPrompts	0x0001
#define delNoPrompts		0x0002

/* Edition constants: */

#define editionFileType	0x0080
#define editionAuxType	0x00000001L

/* Console constants: */

#define cnsNoCr			0x0000
#define cnsAddCr			0x0001

struct EditionRec
{
	Word edtnError;
};
typedef struct EditionRec EditionRec, *EditionRecPtr, **EditionRecHndl;

struct ExtendedRec
{
	Word offset;
	Word iconBottom;
	Word iconMiddle;
	Word iconText;
	Word iconHeight;
	Word iconWidth;
	iconObjHandle iconHandle;
};
typedef struct ExtendedRec ExtendedRec, *ExtendedRecPtr, **ExtendedRecHndl;

struct SelectRec
{
	Handle TextH;
	Handle StyleH;
};
typedef struct SelectRec SelectRec, *SelectRecPtr, **SelectRecHndl;

struct SplitRec
{
	Handle filenameHndl;
	Handle prefixHndl;
};
typedef struct SplitRec SplitRec, *SplitRecPtr, **SplitRecHndl;

extern pascal void AboutMemory (void);

extern pascal void AddMemRec (Word, MemRecHndl, MemRecPtr, Word);

extern pascal void AddSelectMemRec (Word, MemRecHndl, Word);

extern pascal Handle AppendString (LongWord, Word, LongWord, Word, Word);

extern pascal Word Big2SmallMercury (LongWord, LongWord, Word);

extern pascal Word CalculateCRC (Word, Pointer, LongWord);

extern pascal void ClickCount (EventRecordPtr);

extern pascal LongWord CompressLZSS (Pointer, Pointer);

extern pascal Word ConvertQuotes (Word, Pointer, LongWord);

extern pascal Handle ConvertString (LongWord, Word, Word);

extern pascal Word CountNamesHandle (Handle);

extern pascal void cnslAbortInput (Word);

extern pascal void cnslAddTrap (Word, Pointer);

extern pascal void cnslBackspace (void);

extern pascal void cnslBoxPort (void);

extern pascal void cnslCarriageReturn (void);

extern pascal void cnslCenterString (LongWord, Word, Word);

extern pascal void cnslClearAndHome (void);

extern pascal void cnslClearFromBeginningLine (void);

extern pascal void cnslClearFromBeginningPort (void);

extern pascal void cnslClearLine (void);

extern pascal void cnslClearToEndOfLine (void);

extern pascal void cnslClearToEndOfPort (void);

extern pascal void cnslCloseConsole (Word);

extern pascal void cnslDisableMouseText (void);

extern pascal void cnslDLESpaceExpansion (Word);

extern pascal void cnslEnableMouseText (void);

extern pascal Word cnslFindConsole (void);

extern pascal Handle cnslGetDefaultString (Word);

extern pascal Word cnslGetDevNum (void);

extern pascal void cnslGetInputPort (Word, Pointer);

extern pascal Word cnslGetReadMode (Word);

extern pascal Word cnslGetRefNum (void);

extern pascal char cnslGetScreenChar (Word);

extern pascal Handle cnslGetTerminators (Word);

extern pascal void cnslGetTextPort (Word, Pointer);

extern pascal void cnslGetVectors (Word, Pointer *, Pointer *);

extern pascal Word cnslGetWaitStatus (Word);

extern pascal void cnslGoToXY (Word, Word);

extern pascal void cnslHomeCursor (void);

extern pascal void cnslHorizontalScroll (int);

extern pascal void cnslInitVectors (Word);

extern pascal void cnslInverse (void);

extern pascal void cnslLineFeed (void);

extern pascal void cnslMoveCursorRight (void);

extern pascal void cnslMoveCursorUp (void);

extern pascal Word cnslOpenConsole (Word);

extern pascal Handle cnslNewBoxPort (Word, Word, Word, Word);

extern pascal Handle cnslNewBoxString (LongWord, Word);

extern pascal Handle cnslNewPort (Word, Word, Word, Word);

extern pascal void cnslNoOperation (void);

extern pascal void cnslNormal (void);

extern pascal void cnslPopTextPort (void);

extern pascal char cnslReadCharacter (void);

extern pascal void cnslReadString (ResultBuf255Ptr);

extern pascal void cnslResetTrap (Word);

extern pascal void cnslRestoreFromBox (Handle);

extern pascal void cnslRestoreFromPort (Handle);

extern pascal void cnslRestoreTextPort (Word, Pointer);

extern pascal void cnslRingBell (void);

extern pascal void cnslSaveAndResetPort (void);

extern pascal Handle cnslSaveTextPort (Word);

extern pascal void cnslScrollDownOneLine (void);

extern pascal void cnslScrollUpOneLine (void);

extern pascal void cnslSet40Columns (void);

extern pascal void cnslSet80Columns (void);

extern pascal void cnslSetCursorMovement (Word);

extern pascal void cnslSetDefaultString (Word, Pointer);

extern pascal void cnslSetDevNum (Word);

extern pascal void cnslSetHorizontalPosition (Word);

extern pascal void cnslSetInputPort (Word, Pointer);

extern pascal void cnslSetReadMode (Word, Word);

extern pascal void cnslSetRefNum (Word);

extern pascal void cnslSetTerminators (Word, Pointer);

extern pascal void cnslSetTextPort (Pointer);

extern pascal void cnslSetTextPortSize (Word, Word, Word, Word);

extern pascal void cnslSetVerticalPosition (Word);

extern pascal void cnslSetWaitStatus (Word, Word);

extern pascal Word cnslVectorRead (void);

extern pascal void cnslVectorWrite (char);

extern pascal void cnslWriteCharacter (char);

extern pascal void cnslWriteString (LongWord, Word, Word);

extern pascal void cnslWriteText (Pointer, Word, Word);

extern pascal void DecompressLZSS (Pointer, Pointer);

extern pascal Word DelAllPrompt (SFReplyRec2Ptr);

extern pascal Word DelErrorPrompt (SFReplyRec2Ptr, Word);

extern pascal void DeleteFiles (Word);

extern pascal void DeleteFiles320 (Word);

extern pascal void DeleteFiles640 (Word);

extern pascal void DeleteMemRec (Word, MemRecHndl, Word);

extern pascal void DeleteNames (Handle, Pointer, Pointer, Pointer, Word);

extern pascal Word DelLockedPrompt (SFReplyRec2Ptr);

extern pascal void DrawAbout (void);

extern pascal void EditionClose (EditionRecHndl);

extern pascal Word EditionCountScrap (EditionRecHndl);

extern pascal Word EditionGetIndScrap (EditionRecHndl, Word);

extern pascal Handle EditionGetPBGS (EditionRecHndl);

extern pascal Handle EditionGetPBLS (EditionRecHndl);

extern pascal Handle EditionGetPRGS (EditionRecHndl);

extern pascal Handle EditionGetPROG (EditionRecHndl);

extern pascal Handle EditionGetScrap (EditionRecHndl, Word);

extern pascal EditionRecHndl EditionNew (LongWord, Word, Pointer, Pointer,
	GSString255Ptr, GSString255Ptr);

extern pascal EditionRecHndl EditionOpen (LongWord, Word);

extern pascal void EditionPutScrap (EditionRecHndl, Word, Pointer, LongWord);

extern pascal ExtendedRecPtr ExtendedListEntryPtr (Handle, Word);

extern pascal Pointer FillReplyRec (SFReplyRec2Ptr, Handle, Word);

extern pascal MemRecPtr FindMemRecPtr (Word, MemRecHndl, Word);

extern pascal Pointer FindRealPtr (MemRecPtr);

extern pascal Handle GetDirectory (LongWord, Word);

extern pascal void GetMemRec (Word, MemRecHndl, Word, MemRecPtr);

extern pascal Word GetMemRecSize (MemRecHndl, Word);

extern pascal Word GetOpenPrintFlag (void);

extern pascal void GetOpenPrintInfo (Word, SFReplyRec2Ptr);

extern pascal LongWord GetOpenPrintNumber (void);

extern pascal Handle GetRezFilePath (Word, Word);

extern pascal void HorizontalCenter (WindowPtr);

extern pascal void InitAbout (void);

#define LeftString(SourceRef,SourceType,TargetType,SubSize) \
	MidString(SourceRef,SourceType,TargetType,0,SubSize)

extern pascal Handle MidString (LongWord, Word, Word, Word, Word);

extern pascal void ListClickCount (EventRecordPtr);

extern pascal void LZSSConverter (void);

extern pascal void LZSSConverterRead (void);

extern pascal void MemoryRedraw (WindowPtr);

extern pascal Handle MidString (LongWord, Word, Word, Word, Word);

extern pascal void mlCloseResFile (Handle);

extern pascal Word mlCountResources (Handle, LongWord);

extern pascal Word mlCountTypes (Handle);

extern pascal Word mlGetIndResource (Handle, LongWord, Word);

extern pascal LongWord mlGetIndType (Handle, Word);

extern pascal Word mlGetNamedResource (Handle, LongWord, LongWord, Word);

extern pascal Word mlGetOpenFileRefNum (Handle);

extern pascal Word mlGetResAttrs (Handle, LongWord, Word);

extern pascal Word mlGetResFileAttrs (Handle);

extern pascal Handle mlGetResource (Handle, LongWord, Word);

extern pascal Handle mlGetResourceName (Handle, LongWord, Word, Word);

extern pascal LongWord mlGetResourceSizeOnDisk (Handle, LongWord, Word);

extern pascal Handle mlOpenResFile (LongWord, Word);

extern pascal Word mlResError (void);

extern pascal LongWord mlReverseLong (LongWord);

extern pascal Word mlReverseWord (Word);

extern pascal LongWord mlStringToType (LongWord, Word);

extern pascal Handle mlTypeToString (LongWord, Word);

extern pascal void MoveFiles (Word);

extern pascal void MoveNames (Handle, Pointer);

extern pascal Word MveErrorPrompt (SFReplyRec2Ptr, Word);

extern pascal Pointer NewFPT (Word, Word);

extern pascal Word NextMemRec (Word, MemRecHndl, Word);

extern pascal Handle PreferencePath (LongWord, Word, LongWord, Word, Word);

extern pascal Handle PrefixGet (Word, Word);

extern pascal void PrefixSet (LongWord, Word, Word);

extern pascal Word ResetMemRec (Word, MemRecHndl, Word);

extern pascal Handle RightString (LongWord, Word, Word, Word);

extern pascal Word SearchMemRec (Pointer, LongWord, MemRecHndl, Word);

extern pascal void SelectMemRec (Word, MemRecHndl, Word);

extern pascal void SetFuncPtr (Pointer, Word, Pointer);

extern pascal void SortMemRec (Pointer, MemRecHndl, Word);

extern pascal void SplitPathname (LongWord, Word, Word, Word, SplitRecPtr);

extern pascal Word StringCompare (LongWord, Word, LongWord, Word, Word);

extern pascal Word StringLength (LongWord, Word);

extern pascal GSString255Ptr StringListEntryPtr (Handle, Word);

extern pascal Handle StripLeadingSpace (LongWord, Word, Word);

extern pascal void TECleanUndoInfo (Handle);

extern pascal void TEDisposeEventRec (Handle);

extern pascal Word TEEvent (EventRecordPtr, Handle, Handle);

extern pascal Word TEGetFlags (Handle);

extern pascal Word TEGetRedoType (Handle);

extern pascal void TEGetSelectText (SelectRecPtr, Handle);

extern pascal Word TEGetUndoType (Handle);

extern pascal Handle TENewEventRec (void);

extern pascal void TENewUndo (Word, Handle, Handle);

extern pascal void TERedo (Handle, Handle);

extern pascal void TESetFlags (Word, Handle);

extern pascal Word TESpecial (Word, Handle, Handle);

extern pascal void TEUndo (Handle, Handle);

extern pascal void TraverseNames (Handle, Pointer, Word);

extern pascal void UnInitAbout (void);

extern pascal Word ValCancelCheck (void);

extern pascal Word ValErrorPrompt (SFReplyRec2Ptr, Word);

extern pascal void ValidateFiles (Word);

extern pascal void ValidateNames (Handle, Pointer, Pointer);

#endif
