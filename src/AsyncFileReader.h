#ifndef INCLUDE_ASYNC_FILE_READER_H
#define INCLUDE_ASYNC_FILE_READER_H

// 以下はClose()するまで呼び出すスレッドを一致させること
// Read()、Read()呼び出し後のSetupBuffer(),Clear(),SyncRead(),SetPointer()
class CAsyncFileReader
{
public:
    static const int OVERLAPPED_MAX = 128;
    CAsyncFileReader();
    ~CAsyncFileReader();
    bool SetupBuffer(int bufSize, int bufPrefixSize, int bufNum, int olReqUnit = 2);
    bool Open(LPCTSTR path, DWORD dwShareMode);
    void Close();
    void Clear();
    int Read(BYTE **ppBuf);
    int SyncRead(BYTE *pBuf, int numToRead);
    int SyncRead(BYTE **ppBuf);
    bool SetPointer(__int64 distanceToMove, DWORD dwMoveMethod);
    bool IsOpen() const { return m_hFile != INVALID_HANDLE_VALUE; }
    __int64 GetPosition() const { return IsOpen() ? m_posFront : -1; }
    __int64 GetSize() const;
    int GetBufferSize() const { return max((m_olNum-1)*m_bufSize, 0); }
private:
    int OlNext(int x) const { return x + 1 >= m_olNum ? 0 : x + 1; }
    int OlCount() const { return m_olRear + (m_olRear<m_olFront ? m_olNum : 0) - m_olFront; }
    HANDLE m_hFile;
    OVERLAPPED m_ol[OVERLAPPED_MAX];
    HANDLE m_hEvent[OVERLAPPED_MAX];
    BYTE *m_pBuf[OVERLAPPED_MAX];
    int m_xferred[OVERLAPPED_MAX];
    int m_bufSize, m_bufPreSize, m_olNum, m_olReqUnit;
    int m_olRear, m_olFront;
    __int64 m_posRear, m_posFront;
};

#endif // INCLUDE_ASYNC_FILE_READER_H
