#include <Windows.h>
#include <process.h>
#include "BufferedFileReader.h"

CBufferedFileReader::CBufferedFileReader()
    : m_file(nullptr)
    , m_hThread(nullptr)
{
}

CBufferedFileReader::~CBufferedFileReader()
{
    SetFile(nullptr);
}

// ファイルを設定する
void CBufferedFileReader::SetFile(IReadOnlyFile *file, bool fFileSizeFixed)
{
    SetupBuffer(0, 0, 0);
    m_file = file;
    if (m_file) {
        m_fileSize = fFileSizeFixed ? m_file->GetSize() : -1;
    }
}

// バッファを設定する
// bufPreSizeだけ、Read()で返されるポインタより手前にも確保される
bool CBufferedFileReader::SetupBuffer(int bufSize, int bufPreSize, int bufNum)
{
    if (m_hThread) {
        m_fStop = true;
        ::SetEvent(m_hThreadEvent);
        ::WaitForSingleObject(m_hThread, INFINITE);
        ::CloseHandle(m_hThread);
        ::CloseHandle(m_hThreadEvent);
        m_hThread = nullptr;
    }
    m_queue.clear();
    if (m_file && bufSize >= 1 && bufPreSize >= 0 && bufNum >= 1) {
        // 返却状態のものを1つ余分に確保
        for (int i = 0; i < min(bufNum, BUF_NUM_MAX) + 1; ++i) {
            m_queue.resize(m_queue.size() + 1);
            m_queue.back().reserve(bufPreSize + bufSize);
        }
        m_tail = m_queue.begin();
        m_bufSize = bufSize;
        m_bufPreSize = bufPreSize;
        m_fRead = false;
        m_hThreadEvent = ::CreateEvent(nullptr, FALSE, FALSE, nullptr);
        if (m_hThreadEvent) {
            m_fStop = false;
            m_hThread = reinterpret_cast<HANDLE>(::_beginthreadex(nullptr, 0, ReadThread, this, 0, nullptr));
            if (!m_hThread) {
                ::CloseHandle(m_hThreadEvent);
            }
        }
    }
    return m_hThread != nullptr;
}

// 先読みを終了する
void CBufferedFileReader::Flush()
{
    if (m_hThread) {
        CBlockLock lock(&m_lock);
        __int64 rewind = 0;
        while (m_tail != m_queue.begin()) {
            rewind += static_cast<int>((--m_tail)->size()) - m_bufPreSize;
        }
        if (rewind > 0) {
            m_file->SetPointer(-rewind, IReadOnlyFile::MOVE_METHOD_CURRENT);
        }
        m_fRead = false;
    }
}

// 先読みを開始し、結果を1つ受け取る
// 先読み中はSetFile()に渡したfileにアクセスしてはいけない
int CBufferedFileReader::Read(BYTE **ppBuf)
{
    if (m_hThread) {
        CBlockLock lock(&m_lock);
        if (m_tail == m_queue.begin()) {
            int numRead = SyncRead(ppBuf);
            m_fRead = true;
            ::SetEvent(m_hThreadEvent);
            return numRead;
        }
        else {
            m_queue.splice(m_queue.end(), m_queue, m_queue.begin());
            *ppBuf = &m_queue.back().front() + m_bufPreSize;
            ::SetEvent(m_hThreadEvent);
            return static_cast<int>(m_queue.back().size()) - m_bufPreSize;
        }
    }
    return -1;
}

// 先読みを終了し、同期読み込みの結果を1つ受け取る
int CBufferedFileReader::SyncRead(BYTE **ppBuf)
{
    if (m_hThread) {
        Flush();
        m_queue.back().resize(m_bufPreSize + m_bufSize);
        *ppBuf = &m_queue.back()[m_bufPreSize];
        return m_file->Read(*ppBuf, m_bufSize);
    }
    return -1;
}

// 現在のファイルポインタの位置を取得する
__int64 CBufferedFileReader::GetFilePosition() const
{
    if (m_file) {
        CBlockLock lock(&m_lock);
        __int64 rewind = 0;
        if (m_hThread) {
            for (auto it = m_tail; it != m_queue.begin(); ) {
                rewind += static_cast<int>((--it)->size()) - m_bufPreSize;
            }
        }
        return m_file->SetPointer(0, IReadOnlyFile::MOVE_METHOD_CURRENT) - rewind;
    }
    return -1;
}

// 現在のファイルサイズを取得する
__int64 CBufferedFileReader::GetFileSize() const
{
    if (m_file) {
        if (m_fileSize < 0) {
            CBlockLock lock(&m_lock);
            return m_file->GetSize();
        }
        return m_fileSize;
    }
    return -1;
}

unsigned int __stdcall CBufferedFileReader::ReadThread(void *pParam)
{
    CBufferedFileReader &this_ = *static_cast<CBufferedFileReader*>(pParam);
    for (;;) {
        ::WaitForSingleObject(this_.m_hThreadEvent, INFINITE);
        if (this_.m_fStop) {
            break;
        }
        CBlockLock lock(&this_.m_lock);
        if (this_.m_fRead && std::next(this_.m_tail, 1) != this_.m_queue.end()) {
            this_.m_tail->resize(this_.m_bufPreSize + this_.m_bufSize);
            int numRead = this_.m_file->Read(&(*this_.m_tail)[this_.m_bufPreSize], this_.m_bufSize);
            if (numRead >= 0) {
                if (numRead > 0) {
                    ::SetEvent(this_.m_hThreadEvent);
                }
                (this_.m_tail++)->resize(this_.m_bufPreSize + numRead);
            }
        }
    }
    return 0;
}
