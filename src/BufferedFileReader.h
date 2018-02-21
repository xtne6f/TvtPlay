#ifndef INCLUDE_BUFFERED_FILE_READER_H
#define INCLUDE_BUFFERED_FILE_READER_H

#include "ReadOnlyFile.h"
#include "Util.h"
#include <list>

class CBufferedFileReader
{
public:
    static const int BUF_NUM_MAX = 128;
    CBufferedFileReader();
    ~CBufferedFileReader();
    void SetFile(IReadOnlyFile *file, bool fFileSizeFixed = false);
    bool SetupBuffer(int bufSize, int bufPreSize, int bufNum);
    void Flush();
    int Read(BYTE **ppBuf);
    int SyncRead(BYTE **ppBuf);
    __int64 GetFilePosition() const;
    __int64 GetFileSize() const;
    int GetBufferSize() const { return m_hThread ? static_cast<int>(m_queue.size() - 1) * m_bufSize : 0; }
private:
    static unsigned int __stdcall ReadThread(void *pParam);
    IReadOnlyFile *m_file;
    HANDLE m_hThread;
    HANDLE m_hThreadEvent;
    bool m_fStop;
    bool m_fRead;
    std::list<std::vector<BYTE>> m_queue;
    std::list<std::vector<BYTE>>::iterator m_tail;
    int m_bufSize;
    int m_bufPreSize;
    __int64 m_fileSize;
    mutable recursive_mutex_ m_lock;
};

#endif // INCLUDE_BUFFERED_FILE_READER_H
