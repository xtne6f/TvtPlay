#ifndef INCLUDE_PLAYLIST_H
#define INCLUDE_PLAYLIST_H

class CPlaylist
{
public:
    enum SORT_MODE {
        SORT_NONE, SORT_ASC, SORT_DESC, SORT_SHUFFLE,
    };
    struct PLAY_INFO {
        TCHAR path[MAX_PATH];
    };
    CPlaylist();
    int PushBackListOrFile(LPCTSTR path, bool fMovePos);
    bool MoveCurrentToPrev();
    bool MoveCurrentToNext();
    void Sort(SORT_MODE mode);
    void EraseCurrent();
    void ClearWithoutCurrent();
    bool Prev(bool fLoop);
    bool Next(bool fLoop);
    int ToString(TCHAR *pStr, int max, bool fFileNameOnly) const;
    void SetPosition(size_t pos) { if (pos < m_list.size()) m_pos = pos; }
    size_t GetPosition() const { return m_pos; }
    const std::vector<PLAY_INFO>& Get() const { return m_list; }
    static bool IsPlayListFile(LPCTSTR path);
    static bool IsMediaFile(LPCTSTR path);
private:
    int PushBackList(LPCTSTR fullPath);
    std::vector<PLAY_INFO> m_list;
    // 現在位置。m_list.size()未満、ただしempty()のとき0
    size_t m_pos;
};

#endif
