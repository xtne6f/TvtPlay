// BonDriver_UDP互換のパイプ版BonDriver (2012-03-28)
#include <Windows.h>
#ifndef ASSERT
#include <cassert>
#define ASSERT assert
#endif
#define BONSDK_IMPLEMENT
#include "IBonDriver2.h"
#include "BonDriver_Pipe.h"

// パイプ名(制御パイプには"Ctrl"を付加する)
static LPCTSTR PIPE_NAME = TEXT("\\\\.\\pipe\\BonDriver_Pipe%02u");

static HINSTANCE g_hModule;
static IBonDriver *g_pBonThis;

BOOL APIENTRY DllMain(HINSTANCE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        g_hModule = hModule;
        break;
    case DLL_PROCESS_DETACH:
        if (g_pBonThis) {
            ::OutputDebugString(TEXT("BonDriver_Pipe::DllMain(): Driver Is Not Released!\n"));
            g_pBonThis->Release();
        }
        break;
    }
    return TRUE;
}

extern "C" BONAPI IBonDriver * CreateBonDriver(void)
{
    if (!g_pBonThis) g_pBonThis = new CBonDriverPipe;
    return g_pBonThis;
}

extern "C" BONAPI const STRUCT_IBONDRIVER * CreateBonStruct(void)
{
    CBonDriverPipe *pThis = static_cast<CBonDriverPipe*>(CreateBonDriver());
    return &pThis->GetBonStruct2().Initialize(pThis, NULL);
}

CBonDriverPipe::CBonDriverPipe()
    : m_hReadPipe(INVALID_HANDLE_VALUE)
    , m_hReadPipeThread(NULL)
#ifdef EN_CTRL_PIPE
    , m_hCtrlPipe(INVALID_HANDLE_VALUE)
    , m_hCtrlPipeThread(NULL)
#endif
    , m_hOnStreamEvent(NULL)
    , m_hOnPurgeEvent(NULL)
    , m_hQuitEvent(NULL)
    , m_pIoReqBuff(NULL)
    , m_pIoGetReq(NULL)
    , m_dwReadyReqNum(0)
    , m_dwCurChannel(0xFFFFFFFFUL)
    , m_fPause(false)
{
}

CBonDriverPipe::~CBonDriverPipe()
{
    ::OutputDebugString(TEXT("CBonDriverPipe::~CBonDriverPipe()\n"));
    CloseTuner();
    g_pBonThis = NULL;
}

const BOOL CBonDriverPipe::OpenTuner()
{
    // チューナを開いているあいだはGetTsStream()で返すバッファの生存を保障する
    if (!m_pIoReqBuff) m_pIoReqBuff = NewIoReqBuff(ASYNCBUFFSIZE);
    return IsTunerOpening();
}

void CBonDriverPipe::CloseTuner()
{
    ResetChannel();
    DeleteIoReqBuff(m_pIoReqBuff);
    m_pIoReqBuff = NULL;
}

const BOOL CBonDriverPipe::SetChannel(const BYTE bCh)
{
    return SetChannel(0, bCh - 13);
}

const float CBonDriverPipe::GetSignalLevel(void)
{
    // 信号レベルは常に0
    return 0;
}

const DWORD CBonDriverPipe::WaitTsStream(const DWORD dwTimeOut)
{
    if (!m_hOnStreamEvent) return WAIT_ABANDONED;
    return ::WaitForSingleObject(m_hOnStreamEvent, dwTimeOut ? dwTimeOut : INFINITE);
}

const DWORD CBonDriverPipe::GetReadyCount()
{
    lock_recursive_mutex lock(m_reqLock);
#ifdef EN_CTRL_PIPE
    // 制御パイプを使うときは～1秒程度までバッファを満たしておく手法も想定
    // このときステータスの数値が頻繁に変化するのはうっとうしいので引き下げておく
    return m_dwReadyReqNum==0 ? 0 : max((int)m_dwReadyReqNum - ASYNCBUFFSIZE / ASYNCBUFFTIME, 1);
#else
    return m_dwReadyReqNum;
#endif
}

const BOOL CBonDriverPipe::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain)
{
    BYTE *pSrc;
    if (GetTsStream(&pSrc, pdwSize, pdwRemain)) {
        if (*pdwSize) {
            ::memcpy(pDst, pSrc, *pdwSize);
        }
        return TRUE;
    }
    return FALSE;
}

const BOOL CBonDriverPipe::GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain)
{
    lock_recursive_mutex lock(m_reqLock);

    if (m_dwReadyReqNum != 0) {
        // バッファからデータを取り出す
        *ppDst = m_pIoGetReq->buff;
        if (m_fPause) {
            *pdwSize = 0;
        }
        else {
            *pdwSize = m_pIoGetReq->dwSize;
            // バッファ位置を進める
            m_pIoGetReq = m_pIoGetReq->pNext;
            --m_dwReadyReqNum;
        }
#ifdef EN_CTRL_PIPE
        *pdwRemain = m_dwReadyReqNum==0 ? 0 : max((int)m_dwReadyReqNum - ASYNCBUFFSIZE / ASYNCBUFFTIME, 1);
#else
        *pdwRemain = m_dwReadyReqNum;
#endif
        return TRUE;
    }
    else {
        // 取り出し可能なデータがない
        *pdwSize = *pdwRemain = 0;
        return TRUE;
    }
}

void CBonDriverPipe::PurgeTsStream()
{
    ::OutputDebugString(TEXT("CBonDriverPipe::PurgeTsStream()\n"));
    lock_recursive_mutex lock(m_reqLock);

    // 取り出し可能なデータをパージする
    while (m_dwReadyReqNum != 0) {
        m_pIoGetReq = m_pIoGetReq->pNext;
        --m_dwReadyReqNum;
    }
    // 非同期分も(なるべく)スレッドにパージしてもらう
    if (m_hOnPurgeEvent) ::SetEvent(m_hOnPurgeEvent);
}

void CBonDriverPipe::Release()
{
    delete this;
}

LPCTSTR CBonDriverPipe::GetTunerName(void)
{
    // チューナ名を返す
    return TEXT("UDP/Compat");
}

const BOOL CBonDriverPipe::IsTunerOpening(void)
{
    return m_pIoReqBuff ? TRUE : FALSE;
}

LPCTSTR CBonDriverPipe::EnumTuningSpace(const DWORD dwSpace)
{
    // チューニング空間名を返す
    return dwSpace == 0 ? TEXT("UDP/Compat") : NULL;
}

LPCTSTR CBonDriverPipe::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
    // チャンネル名を返す
    if (dwSpace == 0 && dwChannel < PIPE_NODE_NUM) {
        static TCHAR szBuff[32];
        ::wsprintf(szBuff, TEXT("Pipe # %u"), dwChannel);
        return szBuff;
    }
    return NULL;
}

const BOOL CBonDriverPipe::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
    if (!IsTunerOpening() || dwSpace != 0 || dwChannel >= PIPE_NODE_NUM) return FALSE;

    // 一旦リセット
    ResetChannel();

    // パイプ作成
    TCHAR szName[MAX_PATH];
    ::wsprintf(szName, PIPE_NAME, dwChannel);
    m_hReadPipe = ::CreateNamedPipe(szName, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED,
                                    PIPE_TYPE_BYTE, 1, TSDATASIZE, TSDATASIZE, 3000, NULL);
    if (m_hReadPipe == INVALID_HANDLE_VALUE) goto ERROR_EXIT;
#ifdef EN_CTRL_PIPE
    ::lstrcat(szName, TEXT("Ctrl"));
    m_hCtrlPipe = ::CreateNamedPipe(szName, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE, 1,
                                    BON_PIPE_MESSAGE_MAX * sizeof(TCHAR),
                                    BON_PIPE_MESSAGE_MAX * sizeof(TCHAR), 3000, NULL);
    if (m_hCtrlPipe == INVALID_HANDLE_VALUE) goto ERROR_EXIT;
#endif

    // イベント作成
    m_hOnStreamEvent = ::CreateEvent(NULL, FALSE/*自動*/, FALSE, NULL);
    m_hOnPurgeEvent = ::CreateEvent(NULL, FALSE/*自動*/, FALSE, NULL);
    m_hQuitEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!m_hOnStreamEvent || !m_hOnPurgeEvent || !m_hQuitEvent) goto ERROR_EXIT;

    // スレッド開始
    m_hReadPipeThread = ::CreateThread(NULL, 0, ReadPipeThread, this, 0, NULL);
    if (!m_hReadPipeThread) goto ERROR_EXIT;
#ifdef EN_CTRL_PIPE
    m_hCtrlPipeThread = ::CreateThread(NULL, 0, CtrlPipeThread, this, 0, NULL);
    if (!m_hCtrlPipeThread) goto ERROR_EXIT;
    // 大抵休眠しているが、読み取り時は即応してほしい
    ::SetThreadPriority(m_hCtrlPipeThread, THREAD_PRIORITY_ABOVE_NORMAL);
#endif

    // チャンネル情報更新
    m_dwCurChannel = dwChannel;
    return TRUE;
ERROR_EXIT:
    ::OutputDebugString(TEXT("CBonDriverPipe::SetChannel(): Failed\n"));
    ResetChannel();
    return FALSE;
}

void CBonDriverPipe::ResetChannel()
{
    // スレッド終了
    if (m_hQuitEvent) {
        ::SetEvent(m_hQuitEvent);
    }
#ifdef EN_CTRL_PIPE
    if (m_hCtrlPipeThread) {
        if (::WaitForSingleObject(m_hCtrlPipeThread, 5000) != WAIT_OBJECT_0) {
            // スレッド強制終了
            ::OutputDebugString(TEXT("CBonDriverPipe::CloseTuner(): TerminateThread()\n"));
            ::TerminateThread(m_hCtrlPipeThread, 0);
        }
        ::CloseHandle(m_hCtrlPipeThread);
        m_hCtrlPipeThread = NULL;
    }
#endif
    if (m_hReadPipeThread) {
        if (::WaitForSingleObject(m_hReadPipeThread, 5000) != WAIT_OBJECT_0) {
            // スレッド強制終了
            ::OutputDebugString(TEXT("CBonDriverPipe::CloseTuner(): TerminateThread()\n"));
            ::TerminateThread(m_hReadPipeThread, 0);
        }
        ::CloseHandle(m_hReadPipeThread);
        m_hReadPipeThread = NULL;
    }

    // イベント破棄
    if (m_hQuitEvent) ::CloseHandle(m_hQuitEvent);
    if (m_hOnPurgeEvent) ::CloseHandle(m_hOnPurgeEvent);
    if (m_hOnStreamEvent) ::CloseHandle(m_hOnStreamEvent);
    m_hQuitEvent = m_hOnPurgeEvent = m_hOnStreamEvent = NULL;

    // パイプ破棄(通常はスレッド内部で破棄される)
#ifdef EN_CTRL_PIPE
    if (m_hCtrlPipe != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_hCtrlPipe);
        m_hCtrlPipe = INVALID_HANDLE_VALUE;
    }
#endif
    if (m_hReadPipe != INVALID_HANDLE_VALUE) {
        ::CloseHandle(m_hReadPipe);
        m_hReadPipe = INVALID_HANDLE_VALUE;
    }

    // バッファクリア
    lock_recursive_mutex lock(m_reqLock);
    m_pIoGetReq = m_pIoReqBuff;
    m_dwReadyReqNum = 0;

    // チャンネル情報更新
    m_dwCurChannel = 0xFFFFFFFFUL;
}

const DWORD CBonDriverPipe::GetCurSpace(void)
{
    // 現在のチューニング空間を返す
    return 0;
}

const DWORD CBonDriverPipe::GetCurChannel(void)
{
    // 現在のチャンネルを返す
    return m_dwCurChannel;
}

CBonDriverPipe::AsyncIoReq* CBonDriverPipe::NewIoReqBuff(DWORD dwBuffNum)
{
    AsyncIoReq *pBuff = new AsyncIoReq[dwBuffNum];

    // リンク構築
    for (DWORD i = 0; i < dwBuffNum; ++i) {
        pBuff[i].pNext = &pBuff[(i + 1) % dwBuffNum];
    }
    return pBuff;
}

void CBonDriverPipe::DeleteIoReqBuff(AsyncIoReq *pBuff)
{
    delete [] pBuff;
}

static bool WaitEvent(HANDLE hEvent, HANDLE hQuitEvent)
{
    HANDLE hWaits[2] = { hQuitEvent, hEvent };
    return ::WaitForMultipleObjects(2, hWaits, FALSE, INFINITE) == WAIT_OBJECT_0 + 1;
}

static bool WaitSleep(DWORD dwMilliseconds, HANDLE hQuitEvent)
{
    return ::WaitForSingleObject(hQuitEvent, dwMilliseconds) == WAIT_TIMEOUT;
}

DWORD WINAPI CBonDriverPipe::ReadPipeThread(LPVOID pParam)
{
    return static_cast<CBonDriverPipe*>(pParam)->ReadPipeMain();
}

// TSデータ読み取りパイプ用スレッド
DWORD CBonDriverPipe::ReadPipeMain()
{
    enum { CONNECTING, WAITING, READING, PURGING, DISCONNECTING } state = CONNECTING;
    AsyncIoReq *pIoEnq = m_pIoGetReq;
    AsyncIoReq *pIoDeq = m_pIoGetReq;
    DWORD dwBusyReqNum = 0;
    BOOL fSuccess;
    OVERLAPPED ol;
    HANDLE hPipe = m_hReadPipe;
    HANDLE hEvents[REQRESERVNUM] = {0};
    HANDLE hEventStack[REQRESERVNUM];

    // イベントは使い回す
    for (int i = 0; i < REQRESERVNUM; ++i) {
        hEvents[i] = hEventStack[i] = ::CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!hEvents[i]) goto EXIT;
    }

    for (;;) {
        switch (state) {
        case CONNECTING:
            ::memset(&ol, 0, sizeof(ol));
            ol.hEvent = hEvents[0];
            if (!::ConnectNamedPipe(hPipe, &ol)) {
                DWORD dwErr = ::GetLastError();
                if (dwErr == ERROR_PIPE_CONNECTED) {
                    // すでに接続している場合もある
                    state = READING;
                    break;
                }
                else if (dwErr == ERROR_IO_PENDING) {
                    // クライアントの接続を待つ
                    if (!WaitEvent(hEvents[0], m_hQuitEvent)) goto EXIT;
                    DWORD dwXferred;
                    if (::GetOverlappedResult(hPipe, &ol, &dwXferred, FALSE)) {
                        state = READING;
                    }
                    break;
                }
            }
            ASSERT(false);
            ::OutputDebugString(TEXT("CBonDriverPipe::*PipeMain(): Unexpected Error!\n"));
            goto EXIT;
        case WAITING:
            if (!WaitSleep(SLEEP_ON_FULL + 500, m_hQuitEvent)) goto EXIT;
            state = READING;
            break;
        case READING:
            // 可能なだけ非同期リクエストする
            while (dwBusyReqNum != REQRESERVNUM) {
                {
                    lock_recursive_mutex lock(m_reqLock);
                    if (m_dwReadyReqNum + dwBusyReqNum >= ASYNCBUFFSIZE - 1/*GetTsStream()で返す分*/) {
                        // バッファが溢れるのでウェイト
                        state = WAITING;
                        break;
                    }
                }
                // Pipe読み取りを要求スルデス！
                ::memset(&pIoEnq->ol, 0, sizeof(pIoEnq->ol));
                pIoEnq->ol.hEvent = pIoEnq->hEvent = hEventStack[dwBusyReqNum];
                fSuccess = ::ReadFile(hPipe, pIoEnq->buff, TSDATASIZE, &pIoEnq->dwSize, &pIoEnq->ol);
                if (!fSuccess && ::GetLastError() == ERROR_IO_PENDING) {
                    pIoEnq->dwSize = 0; // IO_PENDINGであることを示す
                }
                else if (!fSuccess || !pIoEnq->dwSize) {
                    state = DISCONNECTING;
                    break;
                }
                pIoEnq = pIoEnq->pNext;
                ++dwBusyReqNum;
            }
            if (state != READING) break;

            // 非同期結果待ち
            if (!pIoDeq->dwSize) {
                HANDLE hWaits[3] = { m_hQuitEvent, m_hOnPurgeEvent, pIoDeq->hEvent };
                DWORD dwRet = ::WaitForMultipleObjects(3, hWaits, FALSE, INFINITE);
                if (dwRet != WAIT_OBJECT_0 + 1 && dwRet != WAIT_OBJECT_0 + 2) goto EXIT;
                else if (dwRet == WAIT_OBJECT_0 + 1) {
                    state = PURGING;
                    break;
                }
                fSuccess = ::GetOverlappedResult(hPipe, &pIoDeq->ol, &pIoDeq->dwSize, FALSE);
            }
            else {
                if (::WaitForSingleObject(m_hOnPurgeEvent, 0) == WAIT_OBJECT_0) {
                    state = PURGING;
                    break;
                }
                fSuccess = TRUE;
            }

            // 1つだけ結果を受け取る
            // TODO: 絶妙なタイミングでPurgeTsStream()されると瞬間的に1つだけ
            //       取り出し可能になる場合がある(けど大して問題でもない)
            if (fSuccess && pIoDeq->dwSize) {
                hEventStack[--dwBusyReqNum] = pIoDeq->hEvent;
                pIoDeq = pIoDeq->pNext;
                lock_recursive_mutex lock(m_reqLock);
                ++m_dwReadyReqNum;
                ::SetEvent(m_hOnStreamEvent);
                break;
            }
            state = DISCONNECTING;
            break;
        case PURGING:
            {
                lock_recursive_mutex lock(m_reqLock);
                while (m_dwReadyReqNum != 0) {
                    m_pIoGetReq = m_pIoGetReq->pNext;
                    --m_dwReadyReqNum;
                }
            }
            // 非同期リクエストをキャンセル
            if (!::CancelIo(hPipe)) {
                ASSERT(false);
                state = DISCONNECTING;
                break;
            }
            // 結果待ち->取り出し済みにする
            while (dwBusyReqNum != 0) {
                if (!pIoDeq->dwSize) {
                    fSuccess = ::GetOverlappedResult(hPipe, &pIoDeq->ol, &pIoDeq->dwSize, TRUE);
                    // 結果が既に返っているものは成功する
                    ASSERT(fSuccess || ::GetLastError() == ERROR_OPERATION_ABORTED);
                }
                hEventStack[--dwBusyReqNum] = pIoDeq->hEvent;
                pIoDeq = pIoDeq->pNext;
                lock_recursive_mutex lock(m_reqLock);
                m_pIoGetReq = m_pIoGetReq->pNext;
            }
            state = READING;
            break;
        case DISCONNECTING:
            fSuccess = ::DisconnectNamedPipe(hPipe);
            ASSERT(fSuccess);

            // 結果待ち->0バイトで取り出し可能にする
            while (dwBusyReqNum != 0) {
                pIoDeq->dwSize = 0;
                hEventStack[--dwBusyReqNum] = pIoDeq->hEvent;
                pIoDeq = pIoDeq->pNext;
                lock_recursive_mutex lock(m_reqLock);
                ++m_dwReadyReqNum;
                ::SetEvent(m_hOnStreamEvent);
            }
            state = CONNECTING;
            break;
        default:
            ASSERT(false);
            goto EXIT;
        }
    }
EXIT:
    if (state == WAITING || state == READING) {
        fSuccess = ::DisconnectNamedPipe(hPipe);
        ASSERT(fSuccess);
    }
    ::CloseHandle(hPipe);
    m_hReadPipe = INVALID_HANDLE_VALUE;
    for (int i = 0; i < REQRESERVNUM; ++i) {
        if (hEvents[i]) ::CloseHandle(hEvents[i]);
    }
    return 0;
}

#ifdef EN_CTRL_PIPE

DWORD WINAPI CBonDriverPipe::CtrlPipeThread(LPVOID pParam)
{
    return static_cast<CBonDriverPipe*>(pParam)->CtrlPipeMain();
}

// 制御パイプ用スレッド
DWORD CBonDriverPipe::CtrlPipeMain()
{
    enum { CONNECTING, READING, WRITING, DISCONNECTING } state = CONNECTING;
    TCHAR szRequest[BON_PIPE_MESSAGE_MAX];
    TCHAR szReply[BON_PIPE_MESSAGE_MAX];
    DWORD dwXferred, dwToWrite;
    BOOL fSuccess;
    OVERLAPPED ol;
    HANDLE hPipe = m_hCtrlPipe;
    HANDLE hEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!hEvent) goto EXIT;

    for (;;) {
        switch (state) {
        case CONNECTING:
            ::memset(&ol, 0, sizeof(ol));
            ol.hEvent = hEvent;
            if (!::ConnectNamedPipe(hPipe, &ol)) {
                DWORD dwErr = ::GetLastError();
                if (dwErr == ERROR_PIPE_CONNECTED) {
                    // すでに接続している場合もある
                    state = READING;
                    break;
                }
                else if (dwErr == ERROR_IO_PENDING) {
                    // クライアントの接続を待つ
                    if (!WaitEvent(hEvent, m_hQuitEvent)) goto EXIT;
                    if (::GetOverlappedResult(hPipe, &ol, &dwXferred, FALSE)) {
                        state = READING;
                    }
                    break;
                }
            }
            ASSERT(false);
            ::OutputDebugString(TEXT("CBonDriverPipe::*PipeMain(): Unexpected Error!\n"));
            goto EXIT;
        case READING:
            ::memset(&ol, 0, sizeof(ol));
            ol.hEvent = hEvent;
            fSuccess = ::ReadFile(hPipe, szRequest, sizeof(szRequest), &dwXferred, &ol);
            if (fSuccess && dwXferred >= sizeof(TCHAR)) {
                szRequest[dwXferred/sizeof(TCHAR)-1] = 0;
                state = WRITING;
                break;
            }
            else if (fSuccess || ::GetLastError() != ERROR_IO_PENDING) {
                state = DISCONNECTING;
                break;
            }
            // 非同期結果待ち
            if (!WaitEvent(hEvent, m_hQuitEvent)) goto EXIT;

            fSuccess = ::GetOverlappedResult(hPipe, &ol, &dwXferred, FALSE);
            if (fSuccess && dwXferred >= sizeof(TCHAR)) {
                szRequest[dwXferred/sizeof(TCHAR)-1] = 0;
                state = WRITING;
                break;
            }
            state = DISCONNECTING;
            break;
        case WRITING:
            dwToWrite = (ProcessPipeMessage(szRequest, szReply) + 1) * sizeof(TCHAR);

            ::memset(&ol, 0, sizeof(ol));
            ol.hEvent = hEvent;
            fSuccess = ::WriteFile(hPipe, &szReply, dwToWrite, &dwXferred, &ol);
            if (fSuccess && dwXferred == dwToWrite) {
                state = READING;
                break;
            }
            else if (fSuccess || ::GetLastError() != ERROR_IO_PENDING) {
                state = DISCONNECTING;
                break;
            }
            // 非同期結果待ち
            if (!WaitEvent(hEvent, m_hQuitEvent)) goto EXIT;

            fSuccess = ::GetOverlappedResult(hPipe, &ol, &dwXferred, FALSE);
            if (fSuccess && dwXferred == dwToWrite) {
                state = READING;
                break;
            }
            state = DISCONNECTING;
            break;
        case DISCONNECTING:
            fSuccess = ::DisconnectNamedPipe(hPipe);
            ASSERT(fSuccess);
            state = CONNECTING;
            break;
        default:
            ASSERT(false);
            goto EXIT;
        }
    }
EXIT:
    if (state == READING || state == WRITING) {
        fSuccess = ::DisconnectNamedPipe(hPipe);
        ASSERT(fSuccess);
    }
    ::CloseHandle(hPipe);
    m_hCtrlPipe = INVALID_HANDLE_VALUE;
    if (hEvent) ::CloseHandle(hEvent);
    return 0;
}

DWORD CBonDriverPipe::ProcessPipeMessage(LPCTSTR pszRequest, LPTSTR pszReply)
{
    if (!::lstrcmp(pszRequest, TEXT("PURGE"))) {
        PurgeTsStream();
        // "A "(要求は正しく処理された) + 1文字以上の応答文字列
        ::lstrcpy(pszReply, TEXT("A 1"));
    }
    else if (!::lstrcmp(pszRequest, TEXT("PAUSE 0"))) {
        m_fPause = false;
        ::lstrcpy(pszReply, TEXT("A 1"));
    }
    else if (!::lstrcmp(pszRequest, TEXT("PAUSE 1"))) {
        m_fPause = true;
        ::lstrcpy(pszReply, TEXT("A 1"));
    }
    else if (!::lstrcmp(pszRequest, TEXT("IS_PAUSED"))) {
        ::lstrcpy(pszReply, m_fPause ? TEXT("A 1") : TEXT("A 0"));
    }
    else if (!::lstrcmp(pszRequest, TEXT("GET_READY_STATE"))) {
        lock_recursive_mutex lock(m_reqLock);
        ::wsprintf(pszReply, TEXT("A %c %04u %04u"),
                   m_dwReadyReqNum == 0 ? TEXT('E') :
                   m_dwReadyReqNum < ASYNCBUFFSIZE / ASYNCBUFFTIME ? TEXT('L') :
                   m_dwReadyReqNum < ASYNCBUFFSIZE / ASYNCBUFFTIME * 2 ? TEXT('N') :
                   m_dwReadyReqNum < ASYNCBUFFSIZE - REQRESERVNUM - 1 ? TEXT('H') : TEXT('F'),
                   m_dwReadyReqNum % 10000, ASYNCBUFFSIZE);
    }
    else {
        // "N"(要求は処理されなかった)
        ::lstrcpy(pszReply, TEXT("N"));
    }
    return ::lstrlen(pszReply);
}

#endif // EN_CTRL_PIPE
