// ---- v5.188.49 Join our Discord (owner 2026-11-21: the later list "the Discord ... invite link
// ( https://discord.gg/hSAER5cNMu )"; the earlier plan: a button that opens the modding server's invite).
// Opens the invite in the player's browser / Discord app with Windows' own Windows.System.Launcher.LaunchUriAsync (official
// WinRT API; nothing is sent by the loader itself). Windows wants it called on the app's UI thread, so the call is handed
// to the game's main view dispatcher (CoreApplication.MainView.CoreWindow.Dispatcher.RunAsync) - all from the official
// Windows headers, loaded at run time like pads_init. MOD LIST: Y opens it.
#include <windows.applicationmodel.core.h>
#include <windows.ui.core.h>
#include <windows.system.h>
#include <windows.foundation.h>

static const wchar_t DC_INVITE[] = L"https://discord.gg/hSAER5cNMu";

struct DcHandler : public ABI::Windows::UI::Core::IDispatchedHandler {   // the dispatcher's callback: launch on the UI thread
    LONG ref = 1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **out) override {
        if (id == __uuidof(IUnknown) || id == __uuidof(ABI::Windows::UI::Core::IDispatchedHandler) || id == __uuidof(IAgileObject)) {
            *out = this; AddRef(); return S_OK;
        }
        *out = NULL; return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return (ULONG)InterlockedIncrement(&ref); }
    ULONG STDMETHODCALLTYPE Release() override { LONG r = InterlockedDecrement(&ref); if (!r) delete this; return (ULONG)r; }
    HRESULT STDMETHODCALLTYPE Invoke() override;
};

static PFN_ROFACTORY g_dc_factory;
static PFN_STRREF g_dc_strref;

static HRESULT dc_factory(const wchar_t *cls, REFIID id, void **out) {
    HSTRING_HEADER hh;
    HSTRING hs = NULL;
    HRESULT hr = g_dc_strref(cls, (UINT32)wcslen(cls), &hh, &hs);
    return SUCCEEDED(hr) ? g_dc_factory(hs, id, out) : hr;
}

HRESULT STDMETHODCALLTYPE DcHandler::Invoke() {
    ABI::Windows::Foundation::IUriRuntimeClassFactory *uf = NULL;
    ABI::Windows::Foundation::IUriRuntimeClass *uri = NULL;
    ABI::Windows::System::ILauncherStatics *ls = NULL;
    ABI::Windows::Foundation::IAsyncOperation<bool> *op = NULL;
    HSTRING_HEADER hh;
    HSTRING hs = NULL;
    HRESULT hr = dc_factory(L"Windows.Foundation.Uri", __uuidof(ABI::Windows::Foundation::IUriRuntimeClassFactory), (void **)&uf);
    if (SUCCEEDED(hr)) hr = g_dc_strref(DC_INVITE, (UINT32)wcslen(DC_INVITE), &hh, &hs);
    if (SUCCEEDED(hr)) hr = uf->CreateUri(hs, &uri);
    if (SUCCEEDED(hr)) hr = dc_factory(L"Windows.System.Launcher", __uuidof(ABI::Windows::System::ILauncherStatics), (void **)&ls);
    if (SUCCEEDED(hr)) hr = ls->LaunchUriAsync(uri, &op);
    logf("Discord: invite opened through Windows' Launcher (0x%08lx)", (unsigned long)hr);
    if (op) op->Release();
    if (ls) ls->Release();
    if (uri) uri->Release();
    if (uf) uf->Release();
    return S_OK;
}

static void dc_open_invite() {                                    // any thread: hands the launch to the game's UI thread
    HMODULE c = LoadLibraryW(L"combase.dll");
    g_dc_factory = c ? (PFN_ROFACTORY)GetProcAddress(c, "RoGetActivationFactory") : NULL;
    g_dc_strref = c ? (PFN_STRREF)GetProcAddress(c, "WindowsCreateStringReference") : NULL;
    if (!g_dc_factory || !g_dc_strref) { set_status("Discord: Windows runtime not available"); return; }
    ABI::Windows::ApplicationModel::Core::ICoreImmersiveApplication *app = NULL;
    ABI::Windows::ApplicationModel::Core::ICoreApplicationView *view = NULL;
    ABI::Windows::UI::Core::ICoreWindow *win = NULL;
    ABI::Windows::UI::Core::ICoreDispatcher *disp = NULL;
    ABI::Windows::Foundation::IAsyncAction *act = NULL;
    HRESULT hr = dc_factory(L"Windows.ApplicationModel.Core.CoreApplication",
                            __uuidof(ABI::Windows::ApplicationModel::Core::ICoreImmersiveApplication), (void **)&app);
    if (SUCCEEDED(hr)) hr = app->get_MainView(&view);
    if (SUCCEEDED(hr) && view) hr = view->get_CoreWindow(&win);
    if (SUCCEEDED(hr) && win) hr = win->get_Dispatcher(&disp);
    if (SUCCEEDED(hr) && disp) {
        DcHandler *h = new DcHandler();
        hr = disp->RunAsync(ABI::Windows::UI::Core::CoreDispatcherPriority_Normal, h, &act);
        h->Release();
    } else if (SUCCEEDED(hr)) hr = E_POINTER;
    logf("Discord: invite asked on the game's UI thread (0x%08lx)", (unsigned long)hr);
    set_status(SUCCEEDED(hr) ? "Discord: the invite opens in your browser / Discord app" : "Discord: could not open the invite - discord.gg/hSAER5cNMu");
    if (act) act->Release();
    if (disp) disp->Release();
    if (win) win->Release();
    if (view) view->Release();
    if (app) app->Release();
}
