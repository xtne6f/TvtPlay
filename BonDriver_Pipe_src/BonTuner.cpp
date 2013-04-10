// BonTuner.cpp: CBonTuner クラスのインプリメンテーション
//
//////////////////////////////////////////////////////////////////////

// BonDriver_UDP互換のパイプ版BonDriver
// クライアントはASYNCBUFFTIME秒以上のパイプ書き込みの遅延を回復すべきでない

#include <Windows.h>
#include <assert.h>
#include "BonTuner.h"

#define PIPE_NODE_NUM		10

//////////////////////////////////////////////////////////////////////
// 定数定義
//////////////////////////////////////////////////////////////////////

// ミューテックス名
#define MUTEX_NAME			TEXT("BonDriver_Pipe")

// パイプ名
#define PIPE_NAME			TEXT("\\\\.\\pipe\\BonDriver_Pipe%02lu")

// 受信サイズ
#define TSDATASIZE			48128			// TSデータのサイズ

// FIFOバッファ設定
#define ASYNCBUFFTIME		2											// バッファ長 = 2秒
#define ASYNCBUFFSIZE		( 0x200000 / TSDATASIZE * ASYNCBUFFTIME )	// 平均16Mbpsとする

#define REQRESERVNUM		16				// 非同期リクエスト予約数
#define REQPOLLINGWAIT		10				// 非同期リクエストポーリング間隔(ms)

// 非同期リクエスト状態
#define IORS_IDLE			0x00			// リクエスト空
#define IORS_BUSY			0x01			// リクエスト受信中	
#define IORS_RECV			0x02			// 受信完了、ストア待ち


//////////////////////////////////////////////////////////////////////
// インスタンス生成メソッド
//////////////////////////////////////////////////////////////////////

extern "C" __declspec(dllexport) IBonDriver * CreateBonDriver()
{
	// スタンス生成(既存の場合はインスタンスのポインタを返す)
	return (CBonTuner::m_pThis)? CBonTuner::m_pThis : ((IBonDriver *) new CBonTuner);
}


//////////////////////////////////////////////////////////////////////
// 構築/消滅
//////////////////////////////////////////////////////////////////////

// 静的メンバ初期化
CBonTuner * CBonTuner::m_pThis = NULL;
HINSTANCE CBonTuner::m_hModule = NULL;

CBonTuner::CBonTuner()
	: m_hMutex(NULL)
	, m_pIoReqBuff(NULL)
	, m_pIoPushReq(NULL)
	, m_pIoPopReq(NULL)
	, m_pIoGetReq(NULL)
	, m_dwBusyReqNum(0UL)
	, m_dwReadyReqNum(0UL)
	, m_hPushIoThread(NULL)
	, m_hPopIoThread(NULL)
	, m_hOnStreamEvent(NULL)
	, m_dwCurSpace(0UL)
	, m_dwCurChannel(0xFFFFFFFFUL)
	, m_hPipe(INVALID_HANDLE_VALUE)
{
	m_pThis = this;

	// クリティカルセクション初期化
	::InitializeCriticalSection(&m_CriticalSection);
}

CBonTuner::~CBonTuner()
{
	// 開かれてる場合は閉じる
	CloseTuner();

	// クリティカルセクション削除
	::DeleteCriticalSection(&m_CriticalSection);

	m_pThis = NULL;
}

const BOOL CBonTuner::OpenTuner()
{
	//return SetChannel(0UL,0UL);

	return TRUE;
}

void CBonTuner::CloseTuner()
{
	// スレッド終了要求セット
	m_bLoopIoThread = FALSE;

	// イベント開放
	if (m_hOnStreamEvent) {
		::CloseHandle(m_hOnStreamEvent);
		m_hOnStreamEvent = NULL;
	}

	// スレッド終了
	if(m_hPushIoThread){
		if(::WaitForSingleObject(m_hPushIoThread, 1000) != WAIT_OBJECT_0){
			// スレッド強制終了
			::TerminateThread(m_hPushIoThread, 0);
			::OutputDebugString(TEXT("BonDriver_Pipe: CBonTuner::CloseTuner() ::TerminateThread(m_hPushIoThread)\n"));
			}

		::CloseHandle(m_hPushIoThread);
		m_hPushIoThread = NULL;
		}

	if(m_hPopIoThread){
		if(::WaitForSingleObject(m_hPopIoThread, 1000) != WAIT_OBJECT_0){
			// スレッド強制終了
			::TerminateThread(m_hPopIoThread, 0);
			::OutputDebugString(TEXT("BonDriver_Pipe: CBonTuner::CloseTuner() ::TerminateThread(m_hPopIoThread)\n"));
			}

		::CloseHandle(m_hPopIoThread);
		m_hPopIoThread = NULL;
		}


	// バッファ開放
	FreeIoReqBuff(m_pIoReqBuff);
	m_pIoReqBuff = NULL;
	m_pIoPushReq = NULL;
	m_pIoPopReq = NULL;
	m_pIoGetReq = NULL;

	m_dwBusyReqNum = 0UL;
	m_dwReadyReqNum = 0UL;

		// ドライバクローズ
	if(m_hPipe != INVALID_HANDLE_VALUE){
		::CloseHandle(m_hPipe);
		m_hPipe = INVALID_HANDLE_VALUE;
		}

	// チャンネル初期化
	m_dwCurSpace = 0UL;
	m_dwCurChannel = 0xFFFFFFFFUL;

	// ミューテックス開放
	if(m_hMutex){
		::ReleaseMutex(m_hMutex);
		::CloseHandle(m_hMutex);
		m_hMutex = NULL;
		}
}

const DWORD CBonTuner::WaitTsStream(const DWORD dwTimeOut)
{
	// 終了チェック
	if(!m_hOnStreamEvent || !m_bLoopIoThread)return WAIT_ABANDONED;

	// イベントがシグナル状態になるのを待つ
	const DWORD dwRet = ::WaitForSingleObject(m_hOnStreamEvent, (dwTimeOut)? dwTimeOut : INFINITE);

	switch(dwRet){
		case WAIT_ABANDONED :
			// チューナが閉じられた
			return WAIT_ABANDONED;

		case WAIT_OBJECT_0 :
		case WAIT_TIMEOUT :
			// ストリーム取得可能 or チューナが閉じられた
			return (m_bLoopIoThread)? dwRet : WAIT_ABANDONED;

		case WAIT_FAILED :
		default:
			// 例外
			return WAIT_FAILED;
		}
}

const DWORD CBonTuner::GetReadyCount()
{
	// 取り出し可能TSデータ数を取得する
	return m_dwReadyReqNum;
}

const BOOL CBonTuner::GetTsStream(BYTE *pDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	BYTE *pSrc = NULL;

	// TSデータをバッファから取り出す
	if(GetTsStream(&pSrc, pdwSize, pdwRemain)){
		if(*pdwSize){
			::CopyMemory(pDst, pSrc, *pdwSize);
			}

		return TRUE;
		}

	return FALSE;
}

const BOOL CBonTuner::GetTsStream(BYTE **ppDst, DWORD *pdwSize, DWORD *pdwRemain)
{
	if(!m_pIoGetReq)return FALSE;

	// TSデータをバッファから取り出す
	if(m_dwReadyReqNum){
		if(m_pIoGetReq->dwState == IORS_RECV){

			// データコピー
			*pdwSize = m_pIoGetReq->dwRxdSize;
			*ppDst = m_pIoGetReq->RxdBuff;

			// バッファ位置を進める
			::EnterCriticalSection(&m_CriticalSection);
			m_pIoGetReq = m_pIoGetReq->pNext;
			m_dwReadyReqNum--;
			*pdwRemain = m_dwReadyReqNum;
			::LeaveCriticalSection(&m_CriticalSection);

			return TRUE;
			}
		else{
			// 例外
			return FALSE;
			}
		}
	else{
		// 取り出し可能なデータがない
		*pdwSize = 0;
		*pdwRemain = 0;

		return TRUE;
		}
}

void CBonTuner::PurgeTsStream()
{
	// バッファから取り出し可能データをパージする

	::EnterCriticalSection(&m_CriticalSection);
	m_pIoGetReq = m_pIoPopReq;
	m_dwReadyReqNum = 0;
	::LeaveCriticalSection(&m_CriticalSection);
}

void CBonTuner::Release()
{
	// インスタンス開放
	delete this;
}

LPCTSTR CBonTuner::GetTunerName(void)
{
	// チューナ名を返す
	return TEXT("UDP/Compat");
}

const BOOL CBonTuner::IsTunerOpening(void)
{
	// チューナの使用中の有無を返す(全プロセスを通して)
	HANDLE hMutex = ::OpenMutex(MUTEX_ALL_ACCESS, FALSE, MUTEX_NAME);
	
	if(hMutex){
		// 既にチューナは開かれている
		::CloseHandle(hMutex);
		return TRUE;
		}
	else{
		// チューナは開かれていない
		return FALSE;
		}
}

LPCTSTR CBonTuner::EnumTuningSpace(const DWORD dwSpace)
{
	// 使用可能なチューニング空間を返す
	switch(dwSpace){
		case 0UL :	return TEXT("UDP/Compat");
		default  :	return NULL;
		}
}

LPCTSTR CBonTuner::EnumChannelName(const DWORD dwSpace, const DWORD dwChannel)
{
	// 使用可能なチャンネルを返す
	if(dwSpace > 0 || (dwChannel >= PIPE_NODE_NUM))return NULL;
	static TCHAR buf[32];
	wsprintf(buf,TEXT("Pipe番号 %lu") , dwChannel);
	return buf;
}

const DWORD CBonTuner::GetCurSpace(void)
{
	// 現在のチューニング空間を返す
	return m_dwCurSpace;
}

const DWORD CBonTuner::GetCurChannel(void)
{
	// 現在のチャンネルを返す
	return m_dwCurChannel;
}

CBonTuner::AsyncIoReq * CBonTuner::AllocIoReqBuff(const DWORD dwBuffNum)
{
	if(dwBuffNum < 2)return NULL;

	// メモリを確保する
	AsyncIoReq *pNewBuff = new AsyncIoReq [dwBuffNum];
	if(!pNewBuff)return NULL;

	// ゼロクリア
	::ZeroMemory(pNewBuff, sizeof(AsyncIoReq) * dwBuffNum);

	// リンクを構築する
	DWORD dwIndex;
	for(dwIndex = 0 ; dwIndex < ( dwBuffNum - 1 ) ; dwIndex++){
		pNewBuff[dwIndex].pNext= &pNewBuff[dwIndex + 1];
		}

	pNewBuff[dwIndex].pNext = &pNewBuff[0];

	return pNewBuff;
}

void CBonTuner::FreeIoReqBuff(CBonTuner::AsyncIoReq *pBuff)
{
	if(!pBuff)return;

	// バッファを開放する
	delete [] pBuff;
}

static bool CheckPipeConnection(HANDLE hPipe, HANDLE hEvent, OVERLAPPED *pOl, bool *pbWait)
{
	if (!*pbWait) {
		::ZeroMemory(pOl, sizeof(OVERLAPPED));
		pOl->hEvent = hEvent;
		if (!::ConnectNamedPipe(hPipe, pOl)) {
			// すでに接続されている可能性もある
			DWORD dwErr = ::GetLastError();
			if (dwErr == ERROR_IO_PENDING) *pbWait = true;
			else if (dwErr == ERROR_PIPE_CONNECTED) return true;
		}
	}
	if (*pbWait && HasOverlappedIoCompleted(pOl)) {
		*pbWait = false;
		return true;
	}
	return false;
}

DWORD WINAPI CBonTuner::PushIoThread(LPVOID pParam)
{
	CBonTuner *pThis = (CBonTuner *)pParam;
	bool bConnect = false;
	bool bWait = false;
	HANDLE hEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	OVERLAPPED ol;
	DWORD dwForceSleep = 0;

	assert(pThis->m_hPipe != INVALID_HANDLE_VALUE);

	// ドライバにTSデータリクエストを発行する
	while (pThis->m_bLoopIoThread) {
		// クライアントの接続を調べる
		if (!bConnect) bConnect = CheckPipeConnection(pThis->m_hPipe, hEvent, &ol, &bWait);

		::EnterCriticalSection(&pThis->m_CriticalSection);
		DWORD dwReady = pThis->m_dwReadyReqNum;
		DWORD dwBusy = pThis->m_dwBusyReqNum;
		::LeaveCriticalSection(&pThis->m_CriticalSection);

		if (dwReady >= ASYNCBUFFSIZE - REQRESERVNUM) {
			// バッファが溢れそうなのでウェイト
			dwForceSleep = (ASYNCBUFFTIME * 1000 + 500) / 100;
		}

		if (dwForceSleep) {
			::Sleep(100);
			dwForceSleep--;
		} else if (bConnect && dwBusy < REQRESERVNUM) {
			// リクエスト処理待ちが規定未満なら追加する
			// ドライバにTSデータリクエストを発行する
			if (!pThis->PushIoRequest()) {
				// 接続を解除する
				::DisconnectNamedPipe(pThis->m_hPipe);
				bConnect = false;
			}
		} else {
			// リクエスト処理待ちがフルの場合はウェイト
			::Sleep(REQPOLLINGWAIT);
		}
	}

	if (bConnect) ::DisconnectNamedPipe(pThis->m_hPipe);
	::CloseHandle(hEvent);
	return 0;
}

DWORD WINAPI CBonTuner::PopIoThread(LPVOID pParam)
{
	CBonTuner *pThis = (CBonTuner *)pParam;
	DWORD dwBusy;

	// 処理済リクエストをポーリングしてリクエストを完了させる
	// リクエスト処理待ちが無くなるまでは脱出しない
	do {
		::EnterCriticalSection(&pThis->m_CriticalSection);
		dwBusy = pThis->m_dwBusyReqNum;
		::LeaveCriticalSection(&pThis->m_CriticalSection);

		// 処理済データがあればリクエストを完了する
		if (!dwBusy || !pThis->PopIoRequest()) {
			// リクエスト処理待ちがないor処理未完了の場合はウェイト
			::Sleep(REQPOLLINGWAIT);
		}
	} while (pThis->m_bLoopIoThread || dwBusy);

	return 0;
}

const BOOL CBonTuner::PushIoRequest()
{
	// ドライバに非同期リクエストを発行する

	assert(m_hPipe != INVALID_HANDLE_VALUE);

	// リクエストセット
	m_pIoPushReq->dwRxdSize = 0;

	// イベント設定
	::ZeroMemory(&m_pIoPushReq->OverLapped, sizeof(OVERLAPPED));
	if(!(m_pIoPushReq->OverLapped.hEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL)))return FALSE;
	
	// Pipe読み取りを要求スルデス！
	if (!::ReadFile(m_hPipe, m_pIoPushReq->RxdBuff, sizeof(m_pIoPushReq->RxdBuff), NULL, &m_pIoPushReq->OverLapped) &&
		::GetLastError() != ERROR_IO_PENDING)
	{
		::CloseHandle(m_pIoPushReq->OverLapped.hEvent);
		return FALSE;
	}
	
	m_pIoPushReq->dwState = IORS_BUSY;

	// バッファ状態更新
	::EnterCriticalSection(&m_CriticalSection);
	m_pIoPushReq = m_pIoPushReq->pNext;
	m_dwBusyReqNum++;
	::LeaveCriticalSection(&m_CriticalSection);

	return TRUE;
}

const BOOL CBonTuner::PopIoRequest()
{
	// 非同期リクエストを完了する

	assert(m_hPipe != INVALID_HANDLE_VALUE);

	// 状態チェック
	if(m_pIoPopReq->dwState != IORS_BUSY){
		// 例外
		return TRUE;
		}

	// リクエスト取得
	BOOL bRet = ::GetOverlappedResult(m_hPipe, &m_pIoPopReq->OverLapped, &m_pIoPopReq->dwRxdSize, FALSE);

	// エラーチェック
	if(!bRet){
		if(::GetLastError() == ERROR_IO_INCOMPLETE){
			// 処理未完了
			return FALSE;
		} else {
			m_pIoPopReq->dwRxdSize = 0;
		}
	}

	// イベント削除
	::CloseHandle(m_pIoPopReq->OverLapped.hEvent);

	m_pIoPopReq->dwState = IORS_RECV;

	// バッファ状態更新
	::EnterCriticalSection(&m_CriticalSection);
	m_pIoPopReq = m_pIoPopReq->pNext;
	m_dwBusyReqNum--;
	m_dwReadyReqNum++;
	::LeaveCriticalSection(&m_CriticalSection);
	
	// イベントセット
	::SetEvent(m_hOnStreamEvent);

	return TRUE;
}


// チャンネル設定
const BOOL CBonTuner::SetChannel(const BYTE bCh)
{
	return SetChannel((DWORD)0,(DWORD)bCh-13);
}

// チャンネル設定
const BOOL CBonTuner::SetChannel(const DWORD dwSpace, const DWORD dwChannel)
{
	// 有効なチャンネルか
	if(dwSpace > 0 || (dwChannel >= PIPE_NODE_NUM))
		return FALSE;

	// 一旦クローズ
	CloseTuner();

	// バッファ確保
	if(!(m_pIoReqBuff = AllocIoReqBuff(ASYNCBUFFSIZE))){
		return FALSE;
		}

	// バッファ位置同期
	m_pIoPushReq = m_pIoReqBuff;
	m_pIoPopReq = m_pIoReqBuff;
	m_pIoGetReq = m_pIoReqBuff;
	m_dwBusyReqNum = 0;
	m_dwReadyReqNum = 0;

	try{
		// ドライバオープン
		TCHAR szName[MAX_PATH];
		::wsprintf(szName, PIPE_NAME, dwChannel);
		m_hPipe = ::CreateNamedPipe(szName, PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, PIPE_TYPE_BYTE, 1,
		                            sizeof(m_pIoReqBuff->RxdBuff), sizeof(m_pIoReqBuff->RxdBuff), 3000, NULL);
		if (m_hPipe == INVALID_HANDLE_VALUE) {
			::OutputDebugString(TEXT("BonDriver_Pipe: CBonTuner::OpenTuner() CreateNamedPipe error\n"));
			throw 1UL;
		}

		// イベント作成
		if(!(m_hOnStreamEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL)))throw 2UL;

		// スレッド起動
		DWORD dwPushIoThreadID = 0UL, dwPopIoThreadID = 0UL;
		m_hPushIoThread = ::CreateThread(NULL, 0UL, CBonTuner::PushIoThread, this, CREATE_SUSPENDED, &dwPopIoThreadID);
		m_hPopIoThread = ::CreateThread(NULL, 0UL, CBonTuner::PopIoThread, this, CREATE_SUSPENDED, &dwPushIoThreadID);

		if(!m_hPushIoThread || !m_hPopIoThread){
			if(m_hPushIoThread){
				::TerminateThread(m_hPushIoThread, 0UL);
				::CloseHandle(m_hPushIoThread);
				m_hPushIoThread = NULL;
				}

			if(m_hPopIoThread){
				::TerminateThread(m_hPopIoThread, 0UL);
				::CloseHandle(m_hPopIoThread);
				m_hPopIoThread = NULL;
				}

			throw 3UL;
			}

		// スレッド開始
		m_bLoopIoThread = TRUE;
		if(::ResumeThread(m_hPushIoThread) == 0xFFFFFFFFUL || ::ResumeThread(m_hPopIoThread) == 0xFFFFFFFFUL)throw 4UL;

		// ミューテックス作成
		if(!(m_hMutex = ::CreateMutex(NULL, TRUE, MUTEX_NAME)))throw 5UL;
		}
	catch(const DWORD dwErrorStep){
		// エラー発生
		TCHAR szDebugOut[1024];
		::wsprintf(szDebugOut, TEXT("BonDriver_Pipe: CBonTuner::OpenTuner() dwErrorStep = %lu\n"), dwErrorStep);
		::OutputDebugString(szDebugOut);

		CloseTuner();
		return FALSE;
		}

	// チャンネル情報更新
	m_dwCurSpace = dwSpace;
	m_dwCurChannel = dwChannel;

	// TSデータパージ
	PurgeTsStream();

	return TRUE;
}

// 信号レベル取得(常に0)
const float CBonTuner::GetSignalLevel(void)
{
	return 0;
}
