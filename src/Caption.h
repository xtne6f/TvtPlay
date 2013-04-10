#ifndef INCLUDE_CAPTION_H
#define INCLUDE_CAPTION_H

// 名前衝突的に危ういので接頭辞CP_を付けさせてもらう

#define CP_ERR_FALSE            FALSE   // 汎用エラー
#define CP_NO_ERR               TRUE    // 成功

#define CP_ERR_INIT             10
#define CP_ERR_NOT_INIT         11
#define CP_ERR_SIZE             12

#define CP_ERR_NEED_NEXT_PACKET 13      // 次のTSパケット入れないと解析できない
#define CP_ERR_CAN_NOT_ANALYZ   14      // 本当にTSパケット？解析不可能
#define CP_ERR_NOT_FIRST        15      // 最初のTSパケット未入力
#define CP_ERR_INVALID_PACKET   16      // 本当にTSパケット？パケット飛んで壊れてるかも

#define CP_CHANGE_VERSION       20
#define CP_NO_ERR_TAG_INFO      21
#define CP_NO_ERR_CAPTION       22

typedef enum {
    CP_STR_SMALL = 0,   // SSZ
    CP_STR_MEDIUM,      // MSZ
    CP_STR_NORMAL,      // NSZ
    CP_STR_MICRO,       // SZX 0x60
    CP_STR_HIGH_W,      // SZX 0x41
    CP_STR_WIDTH_W,     // SZX 0x44
    CP_STR_W,           // SZX 0x45
    CP_STR_SPECIAL_1,   // SZX 0x6B
    CP_STR_SPECIAL_2,   // SZX 0x64
} CP_STRING_SIZE;

typedef struct {
    unsigned char ucR;
    unsigned char ucG;
    unsigned char ucB;
    unsigned char ucAlpha;
} CLUT_DAT_DLL;

typedef struct {
    char* pszDecode;
    DWORD wCharSizeMode;
    CLUT_DAT_DLL stCharColor;
    CLUT_DAT_DLL stBackColor;
    CLUT_DAT_DLL stRasterColor;
    BOOL bUnderLine;
    BOOL bShadow;
    BOOL bBold;
    BOOL bItalic;
    BYTE bFlushMode;
    BYTE bAlignment;
    WORD wCharW;
    WORD wCharH;
    WORD wCharHInterval;
    WORD wCharVInterval;
    WORD wAlignment;
} CAPTION_CHAR_DATA_DLL;

typedef struct {
    BOOL bClear;
    WORD wSWFMode;
    WORD wClientX;
    WORD wClientY;
    WORD wClientW;
    WORD wClientH;
    WORD wPosX;
    WORD wPosY;
    WORD wAlignment;
    DWORD dwListCount;
    CAPTION_CHAR_DATA_DLL* pstCharList;
    DWORD dwWaitTime;
} CAPTION_DATA_DLL;

typedef struct {
    unsigned char ucLangTag;
    unsigned char ucDMF;
    unsigned char ucDC;
    char szISOLangCode[4];
    unsigned char ucFormat;
    unsigned char ucTCS;
    unsigned char ucRollupMode;
} LANG_TAG_INFO_DLL;

typedef DWORD (WINAPI InitializeCP)();
typedef DWORD (WINAPI InitializeUNICODE)();
typedef DWORD (WINAPI UnInitializeCP)();
typedef DWORD (WINAPI AddTSPacketCP)(BYTE* pbPacket);
typedef DWORD (WINAPI ClearCP)();
typedef DWORD (WINAPI GetTagInfoCP)(LANG_TAG_INFO_DLL** ppList, DWORD* pdwListCount);
typedef DWORD (WINAPI GetCaptionDataCP)(unsigned char ucLangTag, CAPTION_DATA_DLL** ppList, DWORD* pdwListCount);

#endif // INCLUDE_CAPTION_H
