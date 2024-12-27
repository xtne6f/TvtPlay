#include <Windows.h>
#include "ReadOnlyFile.h"

bool CReadOnlyLocalFile::Open(LPCTSTR path, int flags, const char *&errorMessage)
{
    Close();
    if (flags & OPEN_FLAG_NORMAL) {
        m_fShareWrite = !!(flags & OPEN_FLAG_SHARE_WRITE);
        if (!m_fShareWrite) {
            // リモートのファイルは書き込み中でも共有フラグなしでCreateFile()が成功してしまうため
            if (path[0] == TEXT('\\') && path[1] == TEXT('\\')) {
                // UNC
                return false;
            }
            if (path[0] && path[1] == TEXT(':') && path[2] == TEXT('\\')) {
                TCHAR szDrive[] = {path[0], path[1], path[2], TEXT('\0')};
                if (GetDriveType(szDrive) == DRIVE_REMOTE) {
                    return false;
                }
            }
        }
        m_hFile = ::CreateFile(path, GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_DELETE | (m_fShareWrite ? FILE_SHARE_WRITE : 0),
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    }
    return m_hFile != INVALID_HANDLE_VALUE;
}

void CReadOnlyLocalFile::Close()
{
    if (m_hFile != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_hFile);
        m_hFile = INVALID_HANDLE_VALUE;
    }
}

int CReadOnlyLocalFile::Read(BYTE *pBuf, int numToRead)
{
    DWORD numRead;
    if (m_hFile != INVALID_HANDLE_VALUE && numToRead > 0 &&
        ::ReadFile(m_hFile, pBuf, numToRead, &numRead, nullptr)) {
        return numRead;
    }
    return -1;
}

__int64 CReadOnlyLocalFile::SetPointer(__int64 distanceToMove, MOVE_METHOD moveMethod)
{
    LARGE_INTEGER liMove;
    liMove.QuadPart = distanceToMove;
    if (m_hFile != INVALID_HANDLE_VALUE &&
        ::SetFilePointerEx(m_hFile, liMove, &liMove,
                           moveMethod == MOVE_METHOD_CURRENT ? FILE_CURRENT :
                           moveMethod == MOVE_METHOD_END ? FILE_END : FILE_BEGIN)) {
        return liMove.QuadPart;
    }
    return -1;
}

__int64 CReadOnlyLocalFile::GetSize() const
{
    LARGE_INTEGER liSize;
    if (m_hFile != INVALID_HANDLE_VALUE && ::GetFileSizeEx(m_hFile, &liSize)) {
        return liSize.QuadPart;
    }
    return -1;
}
