/********************************************
* File: Undo.h
*
*
* Copyright Ewen Wannop © 2018
* All Rights Reserved
*
********************************************/

#ifndef __TYPES__
#include <TYPES.h>
#endif

#ifndef __UNDO__
#define __UNDO__

/* Error Codes */
#define undoFailedLogin 0x8401  /* Too many apps logged in */
#define undoAlreadyLoggedIn 0x8402  /* Already logged in */
#define undoNotLoggedIn 0x8403  /* Not logged in */
#define undoNoActiveControl 0x8404  /* No active LineEdit or TextEdit control found */
#define undoNoSavedClip 0x8405  /* No Undo Clip was found */
#define undoClipBlockFull 0x8406  /* Too many controls have clips already saved */
#define undoLowMemory 0x8407  /* Not enough memory to save Clip */
#define undoNoWindowFound 0x8408  /* No open window found */
#define undoFailedCall 0x84FF  /* Generic failure */

typedef struct UndoDataTable {
    Ref eventRecord;
    word undoMenuItemID[2];  /* new/old */
    word cutMenuItemID[2];
    word copyMenuItemID[2];
    word pasteMenuItemID[2];
    word clearMenuItemID[2];
    word selectAllMenuItemID[2];
    word redoMenuItemID[2];
    } UndoDataTable, *UndoDataTablePtr, **UndoDataTableHndl;

typedef struct UndoStatusBuffer {
    word numberLoggedInApps;
    word numberControlsThisApp;
    word numberClipsThisControl;
    word stackPointerThisControl;
    word lastKeyPressThisControl;
    } UndoStatusBuffer, *UndoStatusBufferPtr, **UndoStatusBufferHndl;

extern pascal void UndoBootInit(void) inline(0x0184,dispatcher);
extern pascal void UndoStartUp(void) inline(0x0284,dispatcher);
extern pascal void UndoShutDown(void) inline(0x0384,dispatcher);
extern pascal Word UndoVersion(void) inline(0x0484,dispatcher);
extern pascal void UndoReset(void) inline(0x0584,dispatcher);
extern pascal Word UndoStatus(void) inline(0x0684,dispatcher);
extern pascal void UndoLogIn(Word, UndoDataTablePtr) inline(0x0984,dispatcher);
extern pascal void UndoLogOut(Word) inline(0x0A84,dispatcher);
extern pascal void UndoKeyAction(Word) inline(0x0B84,dispatcher);
extern pascal void UndoMenuAction(Word) inline(0x0C84,dispatcher);
extern pascal void UndoSaveClip(Word) inline(0x0D84,dispatcher);
extern pascal void UndoClearClips(Word) inline(0x0E84,dispatcher);
extern pascal void UndoClear(Word) inline(0x0F84,dispatcher);
extern pascal void UndoClearWindow(Word) inline(0x1084,dispatcher);
extern pascal void UndoLogoutAll(void) inline(0x1184,dispatcher);
extern pascal void UndoClipStatus(Word, UndoStatusBufferPtr, CtlRecHndl) inline(0x1284, dispatcher);

#endif
