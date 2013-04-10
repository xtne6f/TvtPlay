#ifndef INCLUDE_CHAPTER_MAP_H
#define INCLUDE_CHAPTER_MAP_H

#define CHAPTER_NAME_MAX 16

struct CHAPTER_NAME {
    TCHAR val[CHAPTER_NAME_MAX];
    bool IsIn() const { return !::ChrCmpI(val[0], TEXT('i')); }
    bool IsOut() const { return !::ChrCmpI(val[0], TEXT('o')); }
    bool IsX() const { return !::lstrcmpi(IsIn()||IsOut() ? val+1 : val, TEXT("x")); }
    void InvertPrefix(TCHAR c);
};

typedef std::pair<int, CHAPTER_NAME> CHAPTER;

class CChapterMap : public std::map<int, CHAPTER_NAME>
{
    static const int RETRY_LIMIT = 3;
public:
    CChapterMap();
    ~CChapterMap();
    bool Open(LPCTSTR path);
    void Close();
    bool Sync();
    bool Save() const;
    bool IsOpen() const { return m_path[0] != 0; }
    bool NeedToSync() const { return m_hDir != INVALID_HANDLE_VALUE; }
private:
    bool InsertCommand(LPCTSTR p);
    TCHAR m_path[MAX_PATH];
    TCHAR m_longName[MAX_PATH];
    TCHAR m_shortName[MAX_PATH];
    HANDLE m_hDir, m_hEvent;
    bool m_fWritable;
    bool m_fWaiting;
    int m_retryCount;
    OVERLAPPED m_ol;
    BYTE m_buf[2048];
};

#endif // INCLUDE_CHAPTER_MAP_H
