#ifndef INCLUDE_BON_DRIVER_PIPE_H
#define INCLUDE_BON_DRIVER_PIPE_H

#define BON_PIPE_MESSAGE_MAX  128

// 制御パイプを実装するとき定義
#define EN_CTRL_PIPE

class recursive_mutex_
{
public:
    recursive_mutex_() { ::InitializeCriticalSection(&m_cs); }
    ~recursive_mutex_() { ::DeleteCriticalSection(&m_cs); }
    void lock() { ::EnterCriticalSection(&m_cs); }
    void unlock() { ::LeaveCriticalSection(&m_cs); }
private:
    recursive_mutex_(const recursive_mutex_&);
    recursive_mutex_ &operator=(const recursive_mutex_&);
    CRITICAL_SECTION m_cs;
};

class lock_recursive_mutex
{
public:
    lock_recursive_mutex(recursive_mutex_ &mtx) : m_mtx(mtx) { m_mtx.lock(); }
    ~lock_recursive_mutex() { m_mtx.unlock(); }
private:
    lock_recursive_mutex(const lock_recursive_mutex&);
    lock_recursive_mutex &operator=(const lock_recursive_mutex&);
    recursive_mutex_ &m_mtx;
};

class CBonDriverPipe : public IBonDriver2
{
    static const int TSDATASIZE = 256 * 188;    // TSデータをまとめて扱うサイズ
#ifdef EN_CTRL_PIPE
    static const int ASYNCBUFFTIME = 3;         // バッファ長[秒]
#else
    static const int ASYNCBUFFTIME = 2;         // バッファ長[秒]
#endif
    static const int ASYNCBUFFSIZE = 0x200000 / TSDATASIZE * ASYNCBUFFTIME; // バッファ数(平均16Mbpsと仮定)
    static const int REQRESERVNUM = 8;          // 非同期リクエスト数
    static const int PIPE_NODE_NUM = 10;        // チャンネル数
    static const int SLEEP_ON_FULL = 2000;      // バッファが一杯になったときにTSデータ読み取りを中断する時間[ミリ秒]
                                                // クライアントはこれ以上のパイプ書き込みの遅延を回復すべきでない
public:
    CBonDriverPipe();
    ~CBonDriverPipe(); // 派生禁止!
    STRUCT_IBONDRIVER2 &GetBonStruct2() { return m_bonStruct2; }
    // IBonDriver
    const BOOL OpenTuner(void);
    void CloseTuner(void);
    const BOOL SetChannel(const BYTE bCh);
    const float GetSignalLevel(void);
    const DWORD WaitTsStream(const DWORD dwTimeOut = 0);
    const DWORD GetReadyCount(void);
    const BOOL GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain);
    const BOOL GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain);
    void PurgeTsStream(void);
    void Release(void);
    // IBonDriver2
    LPCTSTR GetTunerName(void);
    const BOOL IsTunerOpening(void);
    LPCTSTR EnumTuningSpace(const DWORD dwSpace);
    LPCTSTR EnumChannelName(const DWORD dwSpace, const DWORD dwChannel);
    const BOOL SetChannel(const DWORD dwSpace, const DWORD dwChannel);
    const DWORD GetCurSpace(void);
    const DWORD GetCurChannel(void);
private:
    // I/Oリクエストキューデータ
    struct AsyncIoReq {
        DWORD dwSize;
        OVERLAPPED ol;
        HANDLE hEvent;
        BYTE buff[TSDATASIZE];
        AsyncIoReq *pNext;
    };
    void ResetChannel();
    static AsyncIoReq* NewIoReqBuff(DWORD dwBuffNum);
    static void DeleteIoReqBuff(AsyncIoReq *pBuff);
    static DWORD WINAPI ReadPipeThread(LPVOID pParam);
    DWORD ReadPipeMain();
#ifdef EN_CTRL_PIPE
    static DWORD WINAPI CtrlPipeThread(LPVOID pParam);
    DWORD CtrlPipeMain();
    DWORD ProcessPipeMessage(LPCTSTR pszRequest, LPTSTR pszReply);
#endif

    HANDLE m_hReadPipe;
    HANDLE m_hReadPipeThread;
#ifdef EN_CTRL_PIPE
    HANDLE m_hCtrlPipe;
    HANDLE m_hCtrlPipeThread;
#endif
    HANDLE m_hOnStreamEvent;
    HANDLE m_hOnPurgeEvent;
    HANDLE m_hQuitEvent;
    recursive_mutex_ m_reqLock;
    AsyncIoReq *m_pIoReqBuff;
    AsyncIoReq *m_pIoGetReq;
    DWORD m_dwReadyReqNum;
    DWORD m_dwCurChannel;
    bool m_fPause;
    STRUCT_IBONDRIVER2 m_bonStruct2;
};

#endif // INCLUDE_BON_DRIVER_PIPE_H
