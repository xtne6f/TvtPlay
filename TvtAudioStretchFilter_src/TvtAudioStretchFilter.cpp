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

// {F8BE8BA2-0B9E-4F1F-9DB8-E603BD3EF711}
static const GUID CLSID_TvtAudioStretchFilter = 
{ 0xf8be8ba2, 0xb9e, 0x4f1f, { 0x9d, 0xb8, 0xe6, 0x3, 0xbd, 0x3e, 0xf7, 0x11 } };

#define FILTER_NAME         "TvtAudioStretchFilter"
#define L_FILTER_NAME       L"TvtAudioStretchFilter"

#define WM_ASFLT_STRETCH    (WM_APP + 1)

#define RATE_MIN 0.24f
#define RATE_MAX 8.01f

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
    HRESULT Transform(IMediaSample *pIn, IMediaSample *pOut);
    HRESULT StartStreaming();
    HRESULT StopStreaming();
private:
    void OnStreamChanged(const WAVEFORMATEX *pwfx);
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    ATOM m_atom;
    HWND m_hwnd;
    bool m_fRate, m_fPitch, m_fTempo, m_fMute, m_fChanged;
    float m_rate;
    bool m_fAcceptConv, m_fStereo;
    soundtouch::SoundTouch m_stouch;
    //DWORD m_dwTick;
};

CTvtAudioStretchFilter::CTvtAudioStretchFilter(LPUNKNOWN punk, HRESULT *phr)
    : CTransformFilter(NAME(FILTER_NAME), punk, CLSID_TvtAudioStretchFilter)
    , m_atom(0)
    , m_hwnd(NULL)
    , m_fRate(false)
    , m_fPitch(false)
    , m_fTempo(false)
    , m_fMute(false)
    , m_fChanged(false)
    , m_rate(1.0f)
    , m_fAcceptConv(false)
    , m_fStereo(false)
{
}


CTvtAudioStretchFilter::~CTvtAudioStretchFilter()
{
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
    // 入力形式はPCMのみであること
    if (IsEqualGUID(*mtIn->Type(), MEDIATYPE_Audio) &&
        IsEqualGUID(*mtIn->Subtype(), MEDIASUBTYPE_PCM) &&
        IsEqualGUID(*mtIn->FormatType(), FORMAT_WaveFormatEx))
    {
        return S_OK;
    }
    return VFW_E_TYPE_NOT_ACCEPTED;
}


HRESULT CTvtAudioStretchFilter::CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut)
{
    // 出力形式は入力形式と同じであること
    if (SUCCEEDED(CheckInputType(mtIn)) && *mtIn == *mtOut) {
        return S_OK;
    }
    return VFW_E_TYPE_NOT_ACCEPTED;
}


HRESULT CTvtAudioStretchFilter::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProp)
{
    if (!m_pInput->IsConnected()) return E_UNEXPECTED;

    IMemAllocator *pInAlloc = NULL;
    HRESULT hr = m_pInput->GetAllocator(&pInAlloc);
    if (SUCCEEDED(hr)) {
        ALLOCATOR_PROPERTIES InProps;
        hr = pInAlloc->GetProperties(&InProps);
        if (SUCCEEDED(hr)) {
            // スロー再生のために入力ピンよりも大きなバッファを出力ピンに確保
            pProp->cbBuffer = (int)(InProps.cbBuffer / RATE_MIN) / 4 * 4 + 256;
            pProp->cBuffers = 1;
            pProp->cbAlign  = 4;
        }
        pInAlloc->Release();
    }
    if (FAILED(hr)) return hr;

    // 実際にバッファを確保する
    ALLOCATOR_PROPERTIES Actual;
    hr = pAlloc->SetProperties(pProp, &Actual);
    if (FAILED(hr)) return hr;

    // 確保されたバッファが要求よりも少ないことがある
    if (pProp->cBuffers > Actual.cBuffers ||
        pProp->cbBuffer > Actual.cbBuffer) return E_FAIL;

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


HRESULT CTvtAudioStretchFilter::CompleteConnect(PIN_DIRECTION dir, IPin *pReceivePin)
{
    if (dir != PINDIR_INPUT) return S_OK;
    ASSERT(!m_hwnd);
    
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
        if (!m_atom) return S_FALSE;
    }
    
    m_fRate = m_fPitch = m_fTempo = m_fMute = false;
    m_rate = 1.0f;
    m_fChanged = true;
    
    if (!m_hwnd) {
        // ウインドウ名をシステム全体で一意にする
        TCHAR szName[128];
        ::wsprintf(szName, TEXT("%s,%lu"), TEXT(FILTER_NAME), ::GetCurrentProcessId());

        m_hwnd = ::CreateWindow(MAKEINTATOM(m_atom), szName, WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                HWND_MESSAGE, NULL, ::GetModuleHandle(NULL), this);
        if (!m_hwnd) return S_FALSE;
    }
    
    return S_OK;
}


HRESULT CTvtAudioStretchFilter::BreakConnect(PIN_DIRECTION dir)
{
    if (dir != PINDIR_INPUT) return S_OK;
    ASSERT(m_hwnd);

    if (m_hwnd) {
        ::DestroyWindow(m_hwnd);
        m_hwnd = NULL;
    }
    return S_OK;
}


void CTvtAudioStretchFilter::OnStreamChanged(const WAVEFORMATEX *pwfx)
{
    // 16bitPCM以外は変換しない
    m_fAcceptConv = pwfx->wFormatTag == WAVE_FORMAT_PCM &&
                    pwfx->wBitsPerSample == 16 &&
                    (pwfx->nChannels == 1 || pwfx->nChannels == 2);

    if (m_fAcceptConv) {
        m_stouch.setChannels(pwfx->nChannels);
        m_stouch.setSampleRate(pwfx->nSamplesPerSec);
        m_stouch.clear();
        m_fStereo = pwfx->nChannels == 2;
    }
}


HRESULT CTvtAudioStretchFilter::Transform(IMediaSample *pIn, IMediaSample *pOut)
{
    // メディアタイプの変更を監視
    AM_MEDIA_TYPE *pMediaType = NULL;
    if (pIn->GetMediaType(&pMediaType) == S_OK) {
        if (IsEqualGUID(pMediaType->majortype, MEDIATYPE_Audio) &&
            IsEqualGUID(pMediaType->subtype, MEDIASUBTYPE_PCM) &&
            IsEqualGUID(pMediaType->formattype, FORMAT_WaveFormatEx) &&
            pMediaType->cbFormat >= sizeof(WAVEFORMATEX))
        {
            OnStreamChanged(reinterpret_cast<WAVEFORMATEX*>(pMediaType->pbFormat));
            ::DeleteMediaType(pMediaType);
        }
        else {
            ::DeleteMediaType(pMediaType);
            return VFW_E_INVALIDMEDIATYPE;
        }
    }

    // 本当は排他制御すべき
    bool fChanged = m_fChanged, fMute = m_fMute;
    float rate = m_rate;
    rate = min(max(rate, RATE_MIN), RATE_MAX);
    if (fChanged) {
        bool fRate = m_fRate, fPitch = m_fPitch, fTempo = m_fTempo;
        m_fChanged = false;
        m_stouch.setRate(fRate ? rate : 1.0f);
        m_stouch.setPitch(fPitch ? rate : 1.0f);
        m_stouch.setTempo(fTempo ? rate : 1.0f);
    }

    BYTE *pbSrc, *pbDest;
    pIn->GetPointer(&pbSrc);
    pOut->GetPointer(&pbDest);
    int srcLen = pIn->GetActualDataLength();
    int destLen;

    if (!m_fAcceptConv || rate == 1.0f) {
        ::CopyMemory(pbDest, pbSrc, srcLen);
        destLen = srcLen;
    }
    else {
        // この場合は16bitPCMであると確信してよい
        int bps = m_fStereo ? 4 : 2;
        m_stouch.putSamples((soundtouch::SAMPLETYPE*)pbSrc, srcLen / bps);
        destLen = m_stouch.receiveSamples((soundtouch::SAMPLETYPE*)pbDest, pOut->GetSize() / bps) * bps;
        if (fMute) ::ZeroMemory(pbDest, destLen);
    }

    // ストリームタイムはそのまま
    REFERENCE_TIME TimeStart, TimeEnd;
    if (pIn->GetTime(&TimeStart, &TimeEnd) == NOERROR) {
        pOut->SetTime(&TimeStart, &TimeEnd);
    }
    LONGLONG MediaStart, MediaEnd;
    if(pIn->GetMediaTime(&MediaStart, &MediaEnd) == NOERROR) {
        pOut->SetMediaTime(&MediaStart, &MediaEnd);
    }
    // データサイズは伸縮する
    ASSERT(destLen <= pOut->GetSize());
    pOut->SetActualDataLength(destLen);
    
    return S_OK;
}


HRESULT CTvtAudioStretchFilter::StartStreaming()
{
    OnStreamChanged(reinterpret_cast<WAVEFORMATEX*>(m_pInput->CurrentMediaType().Format()));
    //m_dwTick = ::GetTickCount();
    return S_OK;
}


HRESULT CTvtAudioStretchFilter::StopStreaming()
{
    //TCHAR str[128];
    //::wsprintf(str, TEXT("Time: %lu msec."), ::GetTickCount() - m_dwTick);
    //::MessageBox(NULL, str, NULL, MB_OK);
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
        }
        break;
    case WM_ASFLT_STRETCH:
        {
            // 常速に対する比率をlParamで受け取る
            int num = LOWORD(lParam);
            int den = HIWORD(lParam);
            if (den == 0) return FALSE;
            float rate = num==den ? 1.0f : (float)((double)num / den);
            if (rate < RATE_MIN || RATE_MAX < rate) return FALSE;
            pThis->m_rate = rate;

            // 変換の種類をwParamで受ける
            pThis->m_fRate = (wParam&0x3) == 1;
            pThis->m_fPitch = (wParam&0x3) == 2;
            pThis->m_fTempo = (wParam&0x3) == 3;
            pThis->m_fMute = (wParam&0x4) != 0;
            pThis->m_fChanged = true;
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

BOOL APIENTRY DllMain(HANDLE hModule, DWORD dwReason, LPVOID lpReserved)
{
    return DllEntryPoint((HINSTANCE)(hModule), dwReason, lpReserved);
}
