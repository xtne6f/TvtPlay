#ifndef INCLUDE_CHAPTER_MAP_H
#define INCLUDE_CHAPTER_MAP_H

class CChapterMap
{
    static const int RETRY_LIMIT = 3;
public:
    static const int CHAPTER_POS_MAX = 99*3600000+59*60000+59*1000+999;
    struct CHAPTER {
        // 最終要素のみにNULを必ず格納する
        std::vector<TCHAR> name;
        CHAPTER(LPCTSTR name_ = TEXT("")) : name(name_, name_ + _tcslen(name_) + 1) {}
        bool IsIn() const { return name[0] == TEXT('I') || name[0] == TEXT('i'); }
        bool IsOut() const { return name[0] == TEXT('O') || name[0] == TEXT('o'); }
        bool IsX() const { TCHAR c = name[IsIn() || IsOut() ? 1 : 0]; return c == TEXT('X') || c == TEXT('x'); }
        void SetIn(bool f) { if (f && !IsIn()) name.insert(name.begin(), TEXT('i')); else if (!f && IsIn()) name.erase(name.begin()); }
        void SetOut(bool f) { if (f && !IsOut()) name.insert(name.begin(), TEXT('o')); else if (!f && IsOut()) name.erase(name.begin()); }
        void SetX(bool f) {
            if (f && !IsX()) name.insert(name.begin() + (IsIn() || IsOut() ? 1 : 0), TEXT('x'));
            else if (!f && IsX()) name.erase(name.begin() + (IsIn() || IsOut() ? 1 : 0));
        }
    };
    CChapterMap();
    ~CChapterMap();
    bool Open(LPCTSTR path, LPCTSTR subDirName);
    void Close();
    bool Sync();
    bool Insert(const std::pair<int, CHAPTER> &ch, int pos = -1);
    bool Erase(int pos);
    void ShiftAll(int offset);
    const std::map<int, CHAPTER>& Get() const { return m_map; }
    bool IsOpen() const { return m_path[0] != 0; }
    bool NeedToSync() const { return m_hDir != INVALID_HANDLE_VALUE; }
private:
    bool Save() const;
    bool InsertCommand(LPCTSTR p);
    bool InsertOgmStyleCommand(LPCTSTR p);
    std::map<int, CHAPTER> m_map;
    TCHAR m_path[MAX_PATH];
    HANDLE m_hDir, m_hEvent;
    bool m_fWritable;
    int m_retryCount;
    OVERLAPPED m_ol;
    BYTE m_buf[2048];
};

#endif // INCLUDE_CHAPTER_MAP_H
