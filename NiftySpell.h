
#define NiftySpellName  "\pCCV~NiftySpell~"

#define NiftySpellHello         0xF000
#define NiftySpellTEcheck       0xF001
#define NiftySpellLoadDicts     0xF002
#define NiftySpellUnloadDicts   0xF003
#define NiftySpellCheckWord     0xF004
#define NiftySpellMatchWord     0xF005


/*
        Request:        NiftySpellHello ($F000)
                        (Are You There?)

        Data In:                None
        Data Out:       NiftySpellOut
             Result:    1=success
*/
typedef struct NiftySpellHelloOut {
    word    Result;
} NiftySpellHelloOut;

/*
        Request:        NiftySpellCheckTE ($F001)

        Data In:                NiftySpellCheckTEIn
             checkType:         1=Show NS window and do spell check
                        2=Show NS window only if a bad word is found
             TEhand:    NULL=scan windows for a TE control
                        !NULL=check the TE handle
        Data Out:       NiftySpellCheckTEOut
             recvCount
             result:    1=Complete Ok.
*/
typedef struct NiftySpellCheckTEIn {
    word    checkType;
    handle  TEhand;
} NiftySpellCheckTEIn;

typedef struct NiftySpellCheckTEOut {
    word    recvCount;
    word    result;
} NiftySpellCheckTEOut;

/*
        Request:        NiftySpellLoadDicts ($F002)

        Data In:                None
        Data Out:       NiftySpellLoadDictsOut
             recvCount
             Result:    1=dicts loaded ok, !1=something is wrong.
*/
typedef struct NiftySpellLoadDictsOut {
    word    recvCount;
    word    result;
} NiftySpellLoadDictsOut;

/*
        Request:        NiftySpellUnloadDicts ($F003)

        Data In:        None
        Data Out:       None
*/

/*
        Request:        NiftySpellFindWord ($F004)

        Data In:                pString
        Data Out:       NiftySpellFindWordOut
             recvCount
             foundIn:   1=MainDict, 2=UserDict, 0=NotFound
*/
typedef struct NiftySpellFindWordOut {
    word    recvCount;
    word    foundIn;
} NiftySpellFindWordOut;

/*
        Request:                NiftySpellMatchWord ($F005)

        Data In:                pString
        Data Out:        NiftySpellMatchWordOut
             recvCount
             datahandle: Handle of null terminated space delimited
                         words that may match the passed word
*/
typedef struct NiftySpellMatchWordOut {
    word    recvCount;
    handle  datahandle;
} NiftySpellMatchWordOut; 


