#include <streams.h>
#include <SoundTouch.h>

// このソースのビルドにはSoundTouchライブラリ(http://www.surina.net/soundtouch/)が必要。
// このライブラリのソースを入手し、"STTypes.h"で SOUNDTOUCH_INTEGER_SAMPLES を定義して
// ライブラリをビルドし、このプロジェクトにリンクする。
// 64bitビルドについては #undef SOUNDTOUCH_ALLOW_MMX も必要。_M_X64 で場合分けする。
// さらに、Platform SDKのDirectShow BaseClassesも必要。適当にググってビルドし、
// strmbase.libとwinmm.libとを、このプロジェクトにリンクする。

#if 0 // 同一プロセスからTvtAudioStretchFilterへのメッセージ送信コード
#define ASFLT_FILTER_NAME   TEXT("TvtAudioStretchFilter")

static HWND ASFilterFindWindow()
{
    TCHAR szName[128];
    ::wsprintf(szName, TEXT("%s,%lu"), ASFLT_FILTER_NAME, ::GetCurrentProcessId());
    return ::FindWindowEx(HWND_MESSAGE, NULL, ASFLT_FILTER_NAME, szName);
}

LRESULT ASFilterSendMessage(UINT Msg, WPARAM wParam, LPARAM lParam)
{
    HWND hwnd = ASFilterFindWindow();
    return hwnd ? ::SendMessage(hwnd, Msg, wParam, lParam) : FALSE;
}

BOOL ASFilterPostMessage(UINT Msg, WPARAM wParam, LPARAM lParam)
{
    HWND hwnd = ASFilterFindWindow();
    return hwnd ? ::PostMessage(hwnd, Msg, wParam, lParam) : FALSE;
}
#endif

#ifdef _DEBUG
#define DEBUG_OUT(x) ::OutputDebugString(x)
#else
#define DEBUG_OUT(x)
#endif

// {F8BE8BA2-0B9E-4F1F-9DB8-E603BD3EF711}
static const GUID CLSID_TvtAudioStretchFilter = 
{ 0xf8be8ba2, 0xb9e, 0x4f1f, { 0x9d, 0xb8, 0xe6, 0x3, 0xbd, 0x3e, 0xf7, 0x11 } };

static HINSTANCE g_hinstDLL;

#define FILTER_NAME         "TvtAudioStretchFilter"
#define L_FILTER_NAME       L"TvtAudioStretchFilter"

#define WM_ASFLT_STRETCH    (WM_APP + 1)

#define RATE_MIN 0.24f
#define RATE_MAX 8.01f

// ptrBegin()を公開するため
class CSoundTouch : public soundtouch::SoundTouch
{
public:
    soundtouch::SAMPLETYPE *ptrBegin() { return soundtouch::SoundTouch::ptrBegin(); }
};

// 簡易6ch対応SoundTouch
// 2ch以下のパフォーマンスはオリジナルと同等
class CSoundTouchEx
{
    static const int MAX_OBJ = 3;
public:
    CSoundTouchEx() : m_numChannels(2) { for (int i=0; i<MAX_OBJ; ++i) m_stouch[i].setChannels(2); }
    void setRate(float newRate) { for (int i=0; i<MAX_OBJ; ++i) m_stouch[i].setRate(newRate); }
    void setTempo(float newTempo) { for (int i=0; i<MAX_OBJ; ++i) m_stouch[i].setTempo(newTempo); }
    void setPitch(float newPitch) { for (int i=0; i<MAX_OBJ; ++i) m_stouch[i].setPitch(newPitch); }
    void setSampleRate(uint srate) { for (int i=0; i<MAX_OBJ; ++i) m_stouch[i].setSampleRate(srate); }
    void clear() { for (int i=0; i<MAX_OBJ; ++i) m_stouch[i].clear(); }
    uint getChannels() { return m_numChannels; }
    void setChannels(uint numChannels);
    void putSamples(const soundtouch::SAMPLETYPE *samples, uint numSamples);
    uint receiveSamples(soundtouch::SAMPLETYPE *output, uint maxSamples);
private:
    CSoundTouch m_stouch[MAX_OBJ];
    uint m_numChannels;
};

void CSoundTouchEx::setChannels(uint numChannels)
{
    ASSERT(numChannels <= MAX_OBJ * 2);
    m_stouch[0].setChannels(min(numChannels, 2));
    m_numChannels = numChannels;
}

void CSoundTouchEx::putSamples(const soundtouch::SAMPLETYPE *samples, uint numSamples)
{
    int ch = m_numChannels;
    if (ch <= 2) {
        m_stouch[0].putSamples(samples, numSamples);
    }
    else {
        // 3ch以上は2chごとに分離して入力
        soundtouch::SAMPLETYPE buf[4096];
        for (int rest = numSamples; rest > 0; rest -= _countof(buf)/2) {
            const soundtouch::SAMPLETYPE *in = samples + (numSamples - rest) * ch;
            int num = min(rest, _countof(buf)/2);
            for (int i=0; i < (ch + 1) / 2; ++i) {
                for (int j=0; j < (i*2+1<ch ? 2 : 1); ++j) {
                    for (int k=0; k < num; ++k) buf[j+k*2] = in[i*2+j+k*ch];
                }
                m_stouch[i].putSamples(buf, num);
            }
        }
    }
}

uint CSoundTouchEx::receiveSamples(soundtouch::SAMPLETYPE *output, uint maxSamples)
{
    int ch = m_numChannels;
    if (ch <= 2) {
        return m_stouch[0].receiveSamples(output, maxSamples);
    }
    else {
        // maxSamplesの範囲内で現在取得可能なサンプル数を算出
        int reqSamples = m_stouch[0].numSamples();
        for (int i=1; i < (ch + 1) / 2; ++i) {
            reqSamples = min(reqSamples, (int)m_stouch[i].numSamples());
        }
        reqSamples = min(reqSamples, (int)maxSamples);

        // 内部バッファから直接出力
        for (int i=0; i < (ch + 1) / 2; ++i) {
            const soundtouch::SAMPLETYPE *in = m_stouch[i].ptrBegin();
            for (int j=0; j < (i*2+1<ch ? 2 : 1); ++j) {
                 for (int k=0; k < reqSamples; ++k) output[i*2+j+k*ch] = in[j+k*2];
            }
            m_stouch[i].receiveSamples(reqSamples);
        }
        return reqSamples;
    }
}

class CTvtAudioStretchFilter : public CTransformFilter
{
public:
    CTvtAudioStretchFilter(LPUNKNOWN punk, HRESULT *phr);
    ~CTvtAudioStretchFilter();
    static CUnknown* WINAPI CreateInstance(LPUNKNOWN punk, HRESULT *phr);
    HRESULT CheckInputType(const CMediaType *mtIn);
    HRESULT CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut);
    HRESULT DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProp);
    HRESULT GetMediaType(int iPosition, CMediaType *pMediaType);
    HRESULT CompleteConnect(PIN_DIRECTION dir, IPin *pReceivePin);
    HRESULT BreakConnect(PIN_DIRECTION dir);
    HRESULT SetMediaType(PIN_DIRECTION dir, const CMediaType *pmt);
    HRESULT EndFlush(void);
    HRESULT Receive(IMediaSample *pSample);
    HRESULT Transform(IMediaSample *pIn, IMediaSample *pOut);
private:
    static IPin* GetFilterPin(IBaseFilter *pFilter, PIN_DIRECTION dir);
    void AddExtraFilter(IPin *pReceivePin);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    ATOM m_atom;
    HWND m_hwnd;
    float m_rate;
    bool m_fMute;
    bool m_fAcceptConv;
    bool m_fOutputFormatChanged;
    bool m_fFilterAdded;
    CSoundTouchEx m_stouch;
};


CTvtAudioStretchFilter::CTvtAudioStretchFilter(LPUNKNOWN punk, HRESULT *phr)
    : CTransformFilter(NAME(FILTER_NAME), punk, CLSID_TvtAudioStretchFilter)
    , m_atom(0)
    , m_hwnd(NULL)
    , m_rate(1.0f)
    , m_fMute(false)
    , m_fAcceptConv(false)
    , m_fOutputFormatChanged(false)
    , m_fFilterAdded(false)
{
    DEBUG_OUT(TEXT("CTvtAudioStretchFilter::CTvtAudioStretchFilter()\n"));
}


CTvtAudioStretchFilter::~CTvtAudioStretchFilter()
{
    DEBUG_OUT(TEXT("CTvtAudioStretchFilter::~CTvtAudioStretchFilter()\n"));
    ASSERT(!m_hwnd);
    if (m_atom) ::UnregisterClass(MAKEINTATOM(m_atom), ::GetModuleHandle(NULL));
}


CUnknown* WINAPI CTvtAudioStretchFilter::CreateInstance(LPUNKNOWN punk, HRESULT *phr)
{
    CUnknown *pNewObject = new CTvtAudioStretchFilter(punk, phr);
    if (!pNewObject) *phr = E_OUTOFMEMORY;
    return pNewObject;
}


HRESULT CTvtAudioStretchFilter::CheckInputType(const CMediaType *mtIn)
{
    // 入力はWave(PCM)形式であること
    if (mtIn->majortype == MEDIATYPE_Audio &&
        mtIn->subtype == MEDIASUBTYPE_PCM &&
        mtIn->formattype == FORMAT_WaveFormatEx &&
        mtIn->cbFormat >= sizeof(WAVEFORMATEX) &&
        mtIn->pbFormat)
    {
        WAVEFORMATEX &wfx = *reinterpret_cast<WAVEFORMATEX*>(mtIn->Format());
        if (wfx.wFormatTag == WAVE_FORMAT_PCM || wfx.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
            return S_OK;
        }
    }
    return VFW_E_TYPE_NOT_ACCEPTED;
}


HRESULT CTvtAudioStretchFilter::CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut)
{
    DEBUG_OUT(TEXT("CTvtAudioStretchFilter::CheckTransform()\n"));
    if (SUCCEEDED(CheckInputType(mtIn)) && SUCCEEDED(CheckInputType(mtOut))) {
        if (m_pInput && m_pOutput && m_pInput->IsConnected() && m_pOutput->IsConnected()) {
            // フォーマット変更時に入出力形式が異なる場合もある
            return S_OK;
        }
        else {
            // 接続時は入出力形式が一致すること
            WAVEFORMATEX &wfxIn = *reinterpret_cast<WAVEFORMATEX*>(mtIn->Format());
            WAVEFORMATEX &wfxOut = *reinterpret_cast<WAVEFORMATEX*>(mtOut->Format());
            if (wfxIn.wFormatTag      == wfxOut.wFormatTag &&
                wfxIn.nChannels       == wfxOut.nChannels &&
                wfxIn.nSamplesPerSec  == wfxOut.nSamplesPerSec &&
                wfxIn.nAvgBytesPerSec == wfxOut.nAvgBytesPerSec &&
                wfxIn.nBlockAlign     == wfxOut.nBlockAlign &&
                wfxIn.wBitsPerSample  == wfxOut.wBitsPerSample)
            {
                return S_OK;
            }
        }
    }
    return VFW_E_TYPE_NOT_ACCEPTED;
}


HRESULT CTvtAudioStretchFilter::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProp)
{
    DEBUG_OUT(TEXT("CTvtAudioStretchFilter::DecideBufferSize()\n"));
    if (!m_pInput->IsConnected()) return E_UNEXPECTED;
    CheckPointer(pAlloc, E_POINTER);
    CheckPointer(pProp, E_POINTER);

    IMemAllocator *pInAlloc;
    HRESULT hr = m_pInput->GetAllocator(&pInAlloc);
    if (SUCCEEDED(hr)) {
        ALLOCATOR_PROPERTIES inProp;
        hr = pInAlloc->GetProperties(&inProp);
        if (SUCCEEDED(hr)) {
            // スロー再生のために入力ピンよりも大きなバッファを出力ピンに確保
            pProp->cbBuffer = (int)(inProp.cbBuffer / RATE_MIN) / 4 * 4 + 256;
            pProp->cBuffers = 2;
            pProp->cbAlign = 1;
        }
        pInAlloc->Release();
    }
    if (FAILED(hr)) return hr;

    // 実際にバッファを確保する
    ALLOCATOR_PROPERTIES actual;
    hr = pAlloc->SetProperties(pProp, &actual);
    if (FAILED(hr)) return hr;

    // 確保されたバッファが要求よりも少ないことがある
    if (pProp->cBuffers > actual.cBuffers ||
        pProp->cbBuffer > actual.cbBuffer) return E_FAIL;

    return S_OK;
}


HRESULT CTvtAudioStretchFilter::GetMediaType(int iPosition, CMediaType *pMediaType)
{
    if (!m_pInput->IsConnected()) return E_UNEXPECTED;
    if (iPosition < 0) return E_INVALIDARG;
    if (iPosition > 0) return VFW_S_NO_MORE_ITEMS;

    // 接続された入力と同じ形式のみ出力できる
    *pMediaType = m_pInput->CurrentMediaType();
    return S_OK;
}


#ifdef _DEBUG
static int GetRefCount(IUnknown *pUnk)
{
    if (!pUnk) return 0;
    pUnk->AddRef();
    return pUnk->Release();
}
#endif


// フィルタから指定方向のピンを検索する
// 参考: TVTest_0.7.22r2/DirectShowUtil.cpp
IPin* CTvtAudioStretchFilter::GetFilterPin(IBaseFilter *pFilter, PIN_DIRECTION dir)
{
    IEnumPins *pEnumPins = NULL;
    IPin *pPin, *pRetPin = NULL;

    if (pFilter->EnumPins(&pEnumPins) == S_OK) {
        ULONG cFetched;
        while (!pRetPin && pEnumPins->Next(1, &pPin, &cFetched) == S_OK) {
            PIN_INFO stPin;
            if (pPin->QueryPinInfo(&stPin) == S_OK) {
                if (stPin.dir == dir) {
                    pRetPin = pPin;
                    pRetPin->AddRef();
                }
                if (stPin.pFilter) stPin.pFilter->Release();
            }
            pPin->Release();
        }
        pEnumPins->Release();
    }
    return pRetPin;
}


void CTvtAudioStretchFilter::AddExtraFilter(IPin *pReceivePin)
{
    IFilterGraph *pGraph = GetFilterGraph();
    IBaseFilter *pFilter = NULL;
    IPin *pInput = NULL;
    CMediaType mt;

    ASSERT(m_pOutput && pReceivePin);
    if (m_fFilterAdded || !pGraph) return;

    // 設定ファイルに指定されていれば、追加のフィルタを生成する
    WCHAR szIniPath[MAX_PATH + 4];
    if (::GetModuleFileNameW(g_hinstDLL, szIniPath, MAX_PATH)) {
        // 拡張子リネーム
        WCHAR *p = szIniPath + ::lstrlenW(szIniPath) - 1;
        while (p >= szIniPath && *p != L'\\' && *p != L'.') --p;
        if (p >= szIniPath && *p == L'.') ::lstrcpyW(p, L".ini");
        else ::lstrcatW(szIniPath, L".ini");

        WCHAR szFilterID[64];
        ::GetPrivateProfileStringW(L_FILTER_NAME, L"AddFilter", L"",
                                   szFilterID, _countof(szFilterID), szIniPath);
        if (szFilterID[0]) {
            CLSID clsid;
            if (FAILED(::CLSIDFromString(szFilterID, &clsid)) ||
                FAILED(::CoCreateInstance(clsid, NULL, CLSCTX_INPROC, IID_IBaseFilter,
                                          reinterpret_cast<LPVOID*>(&pFilter))))
            {
                ::MessageBox(NULL, TEXT("追加のフィルタを生成できません。"),
                             TEXT(FILTER_NAME), MB_ICONWARNING);
                pFilter = NULL;
            }
        }
    }
    if (!pFilter) goto EXIT;

    // メディアタイプが受け入れられるか調べる(確実ではない)
    pInput = GetFilterPin(pFilter, PINDIR_INPUT);
    if (m_pOutput->GetMediaType(0, &mt) != S_OK ||
        !pInput || pInput->QueryAccept(&mt) != S_OK)
    {
        ::OutputDebugString(TEXT("CTvtAudioStretchFilter::AddExtraFilter(): Denied!\n"));
        goto EXIT;
    }

    // 追加のフィルタをこのフィルタの出力端に挿入する
    bool fSucceeded = false;
    if (SUCCEEDED(pGraph->AddFilter(pFilter, NULL))) {
        // ConnectDirect()により再帰するため
        m_fFilterAdded = true;
        if (SUCCEEDED(pGraph->Disconnect(pReceivePin)) &&
            SUCCEEDED(pGraph->Disconnect(m_pOutput)))
        {
            HRESULT hr = pGraph->ConnectDirect(m_pOutput, pInput, NULL);
            if (SUCCEEDED(hr)) {
                IPin *pOutput = GetFilterPin(pFilter, PINDIR_OUTPUT);
                if (pOutput) {
                    hr = pGraph->ConnectDirect(pOutput, pReceivePin, NULL);
                    if (SUCCEEDED(hr)) fSucceeded = true;
                    pOutput->Release();
                }
            }
        }
        if (!fSucceeded) {
            HRESULT hr = pGraph->RemoveFilter(pFilter);
            ASSERT(SUCCEEDED(hr));
            // つなぎ直す
            hr = pGraph->ConnectDirect(m_pOutput, pReceivePin, NULL);
            ASSERT(SUCCEEDED(hr));
        }
    }
    if (!fSucceeded) {
        ::OutputDebugString(TEXT("CTvtAudioStretchFilter::AddExtraFilter(): Denied and Reconnected!\n"));
    }

EXIT:
    if (pInput) pInput->Release();
    ASSERT(fSucceeded || GetRefCount(pFilter) <= 1);
    if (pFilter) pFilter->Release();
}


HRESULT CTvtAudioStretchFilter::CompleteConnect(PIN_DIRECTION dir, IPin *pReceivePin)
{
    DEBUG_OUT(TEXT("CTvtAudioStretchFilter::CompleteConnect(): "));
    DEBUG_OUT(dir == PINDIR_OUTPUT ? TEXT("Out\n") : TEXT("In\n"));

    if (dir == PINDIR_OUTPUT) {
        AddExtraFilter(pReceivePin);
        return S_OK;
    }

    if (!m_atom) {
        // ウィンドウクラスを登録
        // ついでにこのフィルタがプロセス内で1つであることを保証する
        WNDCLASS wc;
        wc.style         = 0;
        wc.lpfnWndProc   = WndProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        wc.hInstance     = ::GetModuleHandle(NULL);
        wc.hIcon         = NULL;
        wc.hCursor       = NULL;
        wc.hbrBackground = NULL;
        wc.lpszMenuName  = NULL;
        wc.lpszClassName = TEXT(FILTER_NAME);
        m_atom = ::RegisterClass(&wc);
        if (!m_atom) return E_FAIL;
    }

    ASSERT(!m_hwnd);
    if (!m_hwnd) {
        // ウインドウ名をシステム全体で一意にする
        TCHAR szName[128];
        ::wsprintf(szName, TEXT("%s,%lu"), TEXT(FILTER_NAME), ::GetCurrentProcessId());

        m_hwnd = ::CreateWindow(MAKEINTATOM(m_atom), szName, WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                HWND_MESSAGE, NULL, ::GetModuleHandle(NULL), this);
        if (!m_hwnd) return E_FAIL;
    }
    return S_OK;
}


HRESULT CTvtAudioStretchFilter::BreakConnect(PIN_DIRECTION dir)
{
    DEBUG_OUT(TEXT("CTvtAudioStretchFilter::BreakConnect() "));
    DEBUG_OUT(dir == PINDIR_OUTPUT ? TEXT("Out\n") : TEXT("In\n"));

    if (dir == PINDIR_INPUT && m_hwnd) {
        ::DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
    return S_OK;
}


HRESULT CTvtAudioStretchFilter::SetMediaType(PIN_DIRECTION dir, const CMediaType *pmt)
{
    DEBUG_OUT(TEXT("CTvtAudioStretchFilter::SetMediaType() "));
    DEBUG_OUT(dir == PINDIR_OUTPUT ? TEXT("Out\n") : TEXT("In\n"));

    if (dir == PINDIR_INPUT && SUCCEEDED(CheckInputType(pmt))) {
        CAutoLock lock(&m_csReceive);
        WAVEFORMATEX &wfx = *reinterpret_cast<WAVEFORMATEX*>(pmt->Format());
        // 伸縮可能な形式は16bit,6ch以下
        m_fAcceptConv = wfx.wBitsPerSample == 16 && 1 <= wfx.nChannels && wfx.nChannels <= 6;
        if (m_fAcceptConv) {
            m_stouch.setChannels(wfx.nChannels);
            m_stouch.setSampleRate(wfx.nSamplesPerSec);
            m_stouch.clear();
        }
    }
    return S_OK;
}


HRESULT CTvtAudioStretchFilter::EndFlush(void)
{
    DEBUG_OUT(TEXT("CTvtAudioStretchFilter::EndFlush()\n"));
    CAutoLock lock(&m_csReceive);
    if (m_fAcceptConv) m_stouch.clear();
    return CTransformFilter::EndFlush();
}


HRESULT CTvtAudioStretchFilter::Receive(IMediaSample *pSample)
{
    // m_csReceiveでロックされている
    // メディアタイプの変更を監視
    AM_MEDIA_TYPE *pMtIn;
    if (pSample->GetMediaType(&pMtIn) == S_OK) {
        CMediaType mtIn = *pMtIn;
        ::DeleteMediaType(pMtIn);
        HRESULT hr = CheckInputType(&mtIn);
        if (FAILED(hr)) return hr;

        // 出力側のバッファを再設定
        hr = E_POINTER;
        IPin *pPin = m_pOutput->GetConnected();
        if (pPin) {
            IMemInputPin *pMemInputPin;
            hr = pPin->QueryInterface(IID_IMemInputPin, reinterpret_cast<void**>(&pMemInputPin));
            if (SUCCEEDED(hr)) {
                IMemAllocator *pAlloc;
                hr = pMemInputPin->GetAllocator(&pAlloc);
                if (SUCCEEDED(hr)) {
                    hr = E_FAIL;
                    ALLOCATOR_PROPERTIES prop = {0};
                    if (pAlloc->Decommit() == S_OK &&
                        DecideBufferSize(pAlloc, &prop) == S_OK &&
                        pAlloc->Commit() == S_OK)
                    {
                        hr = S_OK;
                    }
                    pAlloc->Release();
                }
                pMemInputPin->Release();
            }
        }
        if (FAILED(hr)) return hr;

        // ピンのメディアタイプを変更(SetMediaType()が呼ばれる)
        m_pInput->SetMediaType(&mtIn);
        m_pOutput->SetMediaType(&mtIn);
        m_fOutputFormatChanged = true;
    }
    // Transform()が呼ばれる
    return CTransformFilter::Receive(pSample);
}


HRESULT CTvtAudioStretchFilter::Transform(IMediaSample *pIn, IMediaSample *pOut)
{
    if (m_fOutputFormatChanged) {
        // メディアタイプの変更を下流に伝える
        pOut->SetMediaType(&m_pOutput->CurrentMediaType());
        m_fOutputFormatChanged = false;
    }

    BYTE *pbSrc, *pbDest;
    pIn->GetPointer(&pbSrc);
    pOut->GetPointer(&pbDest);
    int srcLen = pIn->GetActualDataLength();
    int destLen = pOut->GetSize();
    ASSERT(srcLen <= destLen);

    if (!m_fAcceptConv || m_rate == 1.0f) {
        destLen = min(srcLen, destLen);
        ::CopyMemory(pbDest, pbSrc, destLen);
    }
    else {
        int bps = m_stouch.getChannels() * 2;
        m_stouch.putSamples((soundtouch::SAMPLETYPE*)pbSrc, srcLen / bps);
        destLen = m_stouch.receiveSamples((soundtouch::SAMPLETYPE*)pbDest, destLen / bps) * bps;
    }
    if (m_fMute) ::ZeroMemory(pbDest, destLen);

    // データサイズを伸縮させる
    pOut->SetActualDataLength(destLen);
    return S_OK;
}


LRESULT CALLBACK CTvtAudioStretchFilter::WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CTvtAudioStretchFilter *pThis = reinterpret_cast<CTvtAudioStretchFilter*>(::GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (uMsg) {
    case WM_CREATE:
        {
            LPCREATESTRUCT pcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
            pThis = reinterpret_cast<CTvtAudioStretchFilter*>(pcs->lpCreateParams);
            ::SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));

            CAutoLock lock(&pThis->m_csReceive);
            pThis->m_stouch.setRate(1.0f);
            pThis->m_stouch.setPitch(1.0f);
            pThis->m_stouch.setTempo(1.0f);
            pThis->m_rate = 1.0f;
            pThis->m_fMute = false;
        }
        break;
    case WM_ASFLT_STRETCH:
        {
            CAutoLock lock(&pThis->m_csReceive);
            if (!pThis->m_fAcceptConv) return FALSE;

            // 常速に対する比率をlParamで受ける
            int num = LOWORD(lParam);
            int den = HIWORD(lParam);
            if (den == 0) return FALSE;
            float rate = num==den ? 1.0f : (float)((double)num / den);
            if (rate < RATE_MIN || RATE_MAX < rate) return FALSE;

            // 変換の種類をwParamで受ける
            pThis->m_stouch.setRate((wParam&0x3) == 1 ? rate : 1.0f);
            pThis->m_stouch.setPitch((wParam&0x3) == 2 ? rate : 1.0f);
            pThis->m_stouch.setTempo((wParam&0x3) == 3 ? rate : 1.0f);
            pThis->m_fMute = (wParam&0x4) != 0;

            // ノイズ防止
            if (pThis->m_rate != 1.0f && rate == 1.0f) pThis->m_stouch.clear();
            pThis->m_rate = rate;
        }
        return TRUE;
    }
    return ::DefWindowProc(hwnd, uMsg, wParam, lParam);
}


//
// Self-registration data structures
//
static const AMOVIESETUP_MEDIATYPE sudPinTypes =
{
    &MEDIATYPE_Audio,           // Major type
    &MEDIASUBTYPE_PCM           // Minor type
};

static const AMOVIESETUP_PIN sudPins[] =
{
    {
        L"",
        FALSE,                  // Is it rendered
        FALSE,                  // Is it an output
        FALSE,                  // Allowed none
        FALSE,                  // Allowed many
        &CLSID_NULL,
        NULL,
        1,                      // Number of types
        &sudPinTypes            // Pin information
    },
    {
        L"",
        FALSE,                  // Is it rendered
        TRUE,                   // Is it an output
        FALSE,                  // Allowed none
        FALSE,                  // Allowed many
        &CLSID_NULL,
        NULL,
        1,                      // Number of types
        &sudPinTypes            // Pin information
    }
};

static const AMOVIESETUP_FILTER afFilterInfo =
{
    &CLSID_TvtAudioStretchFilter,   // Filter CLSID
    L_FILTER_NAME,                  // Filter name
    MERIT_DO_NOT_USE + 1,           // Merit
    2,                              // Number of pin types
    sudPins                         // Pointer to pin information
};

CFactoryTemplate g_Templates[] = {
     {
         L_FILTER_NAME,
         &CLSID_TvtAudioStretchFilter,
         CTvtAudioStretchFilter::CreateInstance,
         NULL,
         &afFilterInfo
     }
};

int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);

//
// Exported entry points for registration and unregistration 
//
STDAPI DllRegisterServer()
{
    return AMovieDllRegisterServer2(TRUE);
}

STDAPI DllUnregisterServer()
{
    return AMovieDllRegisterServer2(FALSE);
}

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hinstDLL = hinstDLL;
    }
    return DllEntryPoint(hinstDLL, fdwReason, lpvReserved);
}
