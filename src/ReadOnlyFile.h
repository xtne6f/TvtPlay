#ifndef INCLUDE_READ_ONLY_FILE_H
#define INCLUDE_READ_ONLY_FILE_H

class IReadOnlyFile
{
public:
    enum {
        OPEN_FLAG_NORMAL = 1,
        OPEN_FLAG_SHARE_WRITE = 2,
    };
    enum MOVE_METHOD {
        MOVE_METHOD_BEGIN,
        MOVE_METHOD_CURRENT,
        MOVE_METHOD_END,
    };
    virtual ~IReadOnlyFile() {}
    virtual bool Open(LPCTSTR path, int flags, LPCTSTR &errorMessage) = 0;
    virtual void Close() = 0;
    virtual int Read(BYTE *pBuf, int numToRead) = 0;
    virtual __int64 SetPointer(__int64 distanceToMove, MOVE_METHOD moveMethod) = 0;
    virtual __int64 GetSize() const = 0;
};

class CReadOnlyLocalFile : public IReadOnlyFile
{
public:
    CReadOnlyLocalFile() : m_hFile(INVALID_HANDLE_VALUE) {}
    ~CReadOnlyLocalFile() { Close(); }
    bool Open(LPCTSTR path, int flags, LPCTSTR &errorMessage);
    void Close();
    int Read(BYTE *pBuf, int numToRead);
    __int64 SetPointer(__int64 distanceToMove, MOVE_METHOD moveMethod);
    __int64 GetSize() const;
private:
    HANDLE m_hFile;
};

#endif // INCLUDE_READ_ONLY_FILE_H
