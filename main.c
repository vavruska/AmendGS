#line 2 "/host/AmendGS/main.c"

#include <types.h>
#include <stdio.h>
#include <stdbool.h>

#include <control.h>
#include <desk.h>
#include <dialog.h>
#include <event.h>
#include <font.h>
#include <finder.h>
#include <gsos.h>
#include <scrap.h>
#include <intmath.h>
#include <lineedit.h>
#include <locator.h>
#include <memory.h>
#include <menu.h>
#include <misctool.h>
#include <orca.h>
#include <quickdraw.h>
#include <qdaux.h>
#include <resources.h>
#include <scrap.h>
#include <stdlib.h>
#include <string.h>
#include <stdfile.h>
#include <textedit.h>
#include <ctype.h>
#include <time.h>
#include <window.h>

#include "misclib.h"
#include "undo.h"

#include "NiftySpell.h"
#include "AmendGS.h"
#include "repo.h"
#include "browser.h"
#include "focusable.h"
#include "settings.h"

word programID;
Ref toolResult;
word quitFlag = 0;
WindowPtr aboutWindow = NULL;
WindowPtr currentWindow = NULL;
bool hasUndo = false;
bool hasNiftySpell = false;
EventRecord    currentEvent;


#define MiscLibBaseID 0x00100000

#pragma databank 1
void DrawWindow(void) {
     DrawControls (GetPort());
}
#pragma databank 0

#pragma databank 1
pascal void mainEventHook(pointer p)
{   
    if (hasUndo) {
        UndoKeyAction(programID);
    }
}
#pragma databank 0

bool about_close(struct focusable *focusable) {
    CloseWindow(focusable->win);

    menuDefaults();
    return true;
}

void about_update_menu(void) {
    static char menuCloseStr[] = "\pClose About";

    SetMItemName(menuCloseStr, FILE_MENU_CLOSE_ID);

    DisableMItem(FILE_MENU_NEW_ID);
    DisableMItem(FILE_MENU_OPEN_ID);
    EnableMItem(FILE_MENU_CLOSE_ID);
    DisableMItem(FILE_MENU_SETTING_ID);

    DisableMItem(EDIT_MENU_UNDO_ID);
    DisableMItem(EDIT_MENU_REDO_ID);
    DisableMItem(EDIT_MENU_CUT_ID);
    DisableMItem(EDIT_MENU_COPY_ID);
    DisableMItem(EDIT_MENU_PASTE_ID);
    DisableMItem(EDIT_MENU_CLEAR_ID);
    DisableMItem(EDIT_MENU_SELECT_ALL_ID);
    DisableMItem(EDIT_MENU_SHOWCLIP_ID);
    DisableMItem(EDIT_MENU_NIFTYSPELL_ID);

    DisableMItem(REPO_MENU_ADD_FILE_ID);
    DisableMItem(REPO_MENU_DISCARD_CHANGES_ID);
    DisableMItem(REPO_MENU_APPLY_PATCH_ID);

    DisableMItem(AMENDMENT_MENU_EDIT_ID);
    DisableMItem(AMENDMENT_MENU_EXPORT_ID);
    DisableMItem(AMENDMENT_MENU_VISUALIZE_ID);

}

void NiftySpell(void) {
    NiftySpellCheckTEIn dataIn;
    NiftySpellCheckTEOut dataOut;
    CtlRecHndl ctl;

    ctl = FindTargetCtl();

    //make sure the active control is a TE control
    if ((long) (*ctl)->ctlProc == editTextControl) {
        dataIn.checkType = 1;
        dataIn.TEhand = (Handle) ctl;

        SendRequest(NiftySpellTEcheck, stopAfterOne + sendToName,
                    (long) &NiftySpellName, (long) &dataIn, (ptr)&dataOut);
    }
}

void checkTEControl(void) {
    CtlRecHndl ctl;

    ctl = FindTargetCtl();

    if ((long)(*ctl)->ctlProc == editTextControl) {
        TERecordHndl teRec = (TERecordHndl) ctl;

        //dont enable spellcheck if the TE is readonly
        if ((*teRec)->textFlags & fReadOnly) {
            DisableMItem(EDIT_MENU_NIFTYSPELL_ID);
        } else {
            EnableMItem(EDIT_MENU_NIFTYSPELL_ID);
        }
    } else {
        DisableMItem(EDIT_MENU_NIFTYSPELL_ID);
    }
}

void about_update(struct focusable *focusable, EventRecord *event) {
    word what = -1;

    if (event != NULL) {
        what = event->what;
    }

    switch (what) {
    case -1:
        break;
    case updateEvt:
        about_update_menu();
        break;
    }
}

void about_init(void) {
    struct focusable *focusable;
    WindowPtr aboutWindow;

    AboutMemory();

    aboutWindow = NewWindow2 (NULL, NULL, &DrawAbout, NULL, refIsResource,
            MiscLibBaseID, rWindParam1);

    focusable = xmalloczero(sizeof(struct focusable), "focusable");
    focusable->cookie = NULL;
    focusable->win = aboutWindow;
    focusable->idle = NULL;
    focusable->update = about_update;
    focusable->mouse_down = NULL;
    focusable->menu = NULL;
    focusable->close = about_close;
    focusable_add(focusable);

    about_update_menu();

}

void doClose(void) {
    struct focusable *focusable;
    WindowPtr front;

    front = FrontWindow();

    if ((focusable = focusable_find(front)) != NULL) {
        focusable_close(focusable);
    }
}

void inMenu(Word itemID, word menuID) {
    struct focusable *focused;

    if (menuID && itemID && (focused = focusable_focused()) &&
        focused->menu && focused->menu(focused, menuID, itemID)) {
    } else {
        InitCursor();
        /* Handle the menu item that was chosen */
        switch (itemID) {
        case APPLE_MENU_ABOUT_ID:
            about_init();
            break;
        case FILE_MENU_QUIT_ID:
            quitFlag = 1;
            break;
        case FILE_MENU_NEW_ID:
        {
            struct repo *repo;
            if ((repo = repo_create())) {
                browser_init(repo);
            }
            break;
        }
        case FILE_MENU_CLOSE_ID:
            doClose();
            break;
        case FILE_MENU_OPEN_ID:
        {
            struct repo *repo;
            if ((repo = repo_open(NULL))) {
                browser_init(repo);
            }
            break;
        }
        case FILE_MENU_SETTING_ID:
            settings_edit();
            break;
        case EDIT_MENU_NIFTYSPELL_ID:
            NiftySpell();
            break;
        case EDIT_MENU_SHOWCLIP_ID:
            ShowClipboard(0x8000, NULL);
            break;
        case EDIT_MENU_SELECT_ALL_ID:
            TESetSelection((Pointer) 0x00000000, (Pointer) 0xFFFFFFFF, NULL);
            break;
        default:
            break;
        }
    }
    if (hasUndo) {
        UndoMenuAction(programID);
    }
    HiliteMenu(0, menuID);

}

void mainEventLoop (void) {
    word           taskCode, n;
    struct focusable *focusable;
    WindowPtr old_port, event_win;

    currentEvent.wmTaskMask = 0x001FFFFFL;

    while (!quitFlag) {
        taskCode = TaskMaster(everyEvent, &currentEvent);

        switch (taskCode) {
        case wInSpecial:
        case wInMenuBar:
            inMenu(currentEvent.wmTaskData, HiWord(currentEvent.wmTaskData));
            break;
        case wInControl:
            if ((focusable = focusable_find(FrontWindow())) != NULL) {
                if (!focusable_show(focusable)) {
                    break;
                }
                if (hasNiftySpell) {
                    checkTEControl();
                }
                if (hasUndo) {
                    mainEventHook(NULL);
                } else {
                    SetMenuBar (GetSysBar());
                    MWSetUpEditMenu();
                }
                if (focusable->mouse_down) {
                    focusable->mouse_down(focusable, &currentEvent);
                }
            }
            break;
		case keyDownEvt :
		case autoKeyEvt :
            break;
        case wInGoAway:
            doClose();
            break;
        case activateEvt:
            focusable = focusable_find(FrontWindow());
            if (focusable && focusable->update)
                    focusable->update(focusable, &currentEvent);
            break;
        case updateEvt:
            event_win = (WindowPtr)currentEvent.message;

            old_port = GetPort();
            SetPort(event_win);
            BeginUpdate(event_win);

            focusable = focusable_find(event_win);
            if (focusable && focusable->update)
                    focusable->update(focusable, &currentEvent);

            EndUpdate(event_win);
            SetPort(old_port);
            break;
        case inNull:
            for (n = 0; n < nfocusables; n++) {
                if (focusables[n]->idle) {
                            focusables[n]->idle(focusables[n], &currentEvent);
                    }
            }
            break;
        default:
            break;
        }
    }
    /* Re-init the cursor in case it was an I-Beam */
    InitCursor();

    if (hasUndo) {
        UndoLogOut(programID);
    }
}

void checkNiftySpell(void) {
    NiftySpellHelloOut dataOut;

    SendRequest(NiftySpellHello, stopAfterOne + sendToName, 
                (long) &NiftySpellName, (long) NULL, (ptr)&dataOut);

    if (dataOut.Result) {
        hasNiftySpell = true;
    }
}

void configureEditMenu(void) {
    UndoDataTable undoTable;

    if (hasUndo) {
        undoTable.eventRecord = (Ref) &currentEvent;
        undoTable.undoMenuItemID[0]   = UndoUndoMenuItemID;   /* New */
        undoTable.undoMenuItemID[1]   = EDIT_MENU_UNDO_ID;   /* Old */
        undoTable.cutMenuItemID[0]    = UndoCutMenuItemID;
        undoTable.cutMenuItemID[1]    = EDIT_MENU_CUT_ID;
        undoTable.copyMenuItemID[0]   = UndoCopyMenuItemID;
        undoTable.copyMenuItemID[1]   = EDIT_MENU_COPY_ID;
        undoTable.pasteMenuItemID[0]  = UndoPasteMenuItemID;
        undoTable.pasteMenuItemID[1]  = EDIT_MENU_PASTE_ID;
        undoTable.clearMenuItemID[0]  = UndoClearMenuItemID;
        undoTable.clearMenuItemID[1]  = EDIT_MENU_CLEAR_ID;
        undoTable.selectAllMenuItemID[0] = UndoSelectAllMenuItemID;
        undoTable.selectAllMenuItemID[1] = EDIT_MENU_SELECT_ALL_ID;
        undoTable.redoMenuItemID[0]   = UndoRedoMenuItemID;
        undoTable.redoMenuItemID[1]   = EDIT_MENU_REDO_ID;
        UndoLogIn(programID, &undoTable);
    }
}

void initialize (void) {
    MenuBarRecHndl BarHandle;

    /* Create the new menu bar */
    BarHandle = NewMenuBar2 (refIsResource, MAIN_MENUBAR_ID, NULL);
    SetSysBar (BarHandle);
    SetMenuBar (GetSysBar());

    /* Add desk accessories to the apple menu */
    FixAppleMenu (APPLE_MENU_ID);

    /* Calculate the menu bar dimensions */
    FixMenuBar ();

    /* Draw the new menu bar */
    DrawMenuBar ();

    configureEditMenu();

    settings_load();
    /* Set initial program variables */
    quitFlag = 0;

    InitAbout();
    checkNiftySpell();

    if (hasUndo) {
        MWSetUpEditMenu();
    }
}

void initUndo(void) {
    hasUndo = false;
    LoadOneTool(0x0084, 0x0103);
    if (!toolerror()) {
        UndoStartUp();
        if (!toolerror()) {
            hasUndo = true;
        }
    }
}

void undoUndo(void) {
    if (hasUndo) {
        UndoLogoutAll();
        UndoShutDown();
        UnloadOneTool(0x0084);
    }
}

void menuDefaults(void)
{
    static char menuCloseStr[] = "\pClose Repo";

    SetMItemName(menuCloseStr, FILE_MENU_CLOSE_ID);

    EnableMItem(FILE_MENU_OPEN_ID);
    EnableMItem(FILE_MENU_NEW_ID);
    EnableMItem(FILE_MENU_CLOSE_ID);
    EnableMItem(FILE_MENU_SETTING_ID);
	DisableMItem(EDIT_MENU_UNDO_ID);
	DisableMItem(EDIT_MENU_REDO_ID);
	DisableMItem(EDIT_MENU_CUT_ID);
	DisableMItem(EDIT_MENU_COPY_ID);
    DisableMItem(EDIT_MENU_PASTE_ID);
    DisableMItem(EDIT_MENU_CLEAR_ID);
    EnableMItem(EDIT_MENU_SHOWCLIP_ID);
    DisableMItem(EDIT_MENU_NIFTYSPELL_ID);

	DisableMItem(REPO_MENU_ADD_FILE_ID);
	DisableMItem(REPO_MENU_DISCARD_CHANGES_ID);
	DisableMItem(REPO_MENU_APPLY_PATCH_ID);

	DisableMItem(AMENDMENT_MENU_EDIT_ID);
	DisableMItem(AMENDMENT_MENU_EXPORT_ID);
    DisableMItem(AMENDMENT_MENU_VISUALIZE_ID);
}

void CheckMessages(void) {
    Handle message;
    MessageRecGSPtr messageRec;

    message=NewHandle(0l, (word) programID, (word) 0x0018, NULL);
    MessageCenter(getMessage, fileInfoTypeGS, message);
    while (!toolerror()) {
        messageRec = (MessageRecGSPtr)*message;
        if (!messageRec->printFlag) {
            if (messageRec->fileNames[0].length) {
                Str255 path;
                struct repo *repo;
                path.textLength = messageRec->fileNames[0].length;
                strncpy(path.text, messageRec->fileNames[0].text, messageRec->fileNames[0].length);
                repo = repo_open(&path);
                if (repo) {
                    browser_init(repo);
                }
            }
        }
        MessageCenter(deleteMessage, fileInfoTypeGS, NULL);
        MessageCenter(getMessage, fileInfoTypeGS, message);
    }

}

int main(void) {

    TLStartUp();

    programID = MMStartUp();

    toolResult = StartUpTools(programID, refIsResource, ToolsNeededID);
    if (!toolerror()) {
        //initUndo();
    }

    if (!toolerror()) {

         WaitCursor();     
         initialize();
         menuDefaults();
         InitCursor();
         CheckMessages();

         mainEventLoop();

         CloseAllNDAs();

         UnInitAbout();
    }

    undoUndo();

    ShutDownTools(refIsHandle, toolResult);

    MMShutDown(programID);
    TLShutDown();
}

