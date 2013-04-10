#ifndef INCLUDE_PLAYLIST_H
#define INCLUDE_PLAYLIST_H

struct PLAY_INFO {
    TCHAR path[MAX_PATH];
    bool fWork;
};

class CPlaylist : public std::vector<PLAY_INFO>
{
public:
    CPlaylist();
    int PushBackListOrFile(LPCTSTR path, bool fMovePos);
    bool MoveCurrentToPrev();
    bool MoveCurrentToNext();
    bool Sort(bool fDesc);
    bool EraseCurrent();
    bool ClearWithoutCurrent();
    bool Prev(bool fLoop);
    bool Next(bool fLoop);
    int ToString(TCHAR *pStr, int max, bool fFileNameOnly) const;
    void SetPosition(size_type pos) { m_pos = pos; }
    size_type GetPosition() const { return m_pos; }
private:
    int PushBackList(LPCTSTR fullPath);
    size_t m_pos;
};

#endif
