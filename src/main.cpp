// Anvil — a native Windows (Win32 + WebView2) BYOK agentic coding app.
// The C++ host owns the window + filesystem + command execution; the UI is
// HTML/JS rendered in WebView2. Tools are exposed to JS via window.chrome.webview.
// (c) 2026 Abhyudaya Mishra. All rights reserved. Proprietary — see LICENSE.

// --- Dev toggle ---------------------------------------------------------
//  ANVIL_DEV 1  -> load UI from ./ui/index.html (edit + press F5, no rebuild)
//  ANVIL_DEV 0  -> use the embedded UI in ui_html.h (single-file release)
#define ANVIL_DEV 1
// -----------------------------------------------------------------------

#include <windows.h>
#include <wrl.h>
#include <shobjidl.h>
#include <WebView2.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "ui_html.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

using namespace Microsoft::WRL;
using json = nlohmann::json;

static HWND g_hWnd = nullptr;
static ComPtr<ICoreWebView2Controller> g_controller;
static ComPtr<ICoreWebView2> g_webview;

// ---------- string helpers ----------
static std::wstring widen(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
  return w;
}
static std::string narrow(const std::wstring& w) {
  if (w.empty()) return "";
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
  return s;
}

// Find the ui/ folder by walking up from the exe location (dev mode).
static std::wstring uiFolder() {
  wchar_t buf[MAX_PATH];
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::wstring dir(buf);
  auto slash = dir.find_last_of(L"\\/");
  if (slash != std::wstring::npos) dir = dir.substr(0, slash);
  for (int i = 0; i < 6; ++i) {
    std::wstring cand = dir + L"\\ui";
    if (GetFileAttributesW((cand + L"\\index.html").c_str()) != INVALID_FILE_ATTRIBUTES) return cand;
    auto s = dir.find_last_of(L"\\/");
    if (s == std::wstring::npos) break;
    dir = dir.substr(0, s);
  }
  return dir + L"\\ui";
}

// ---------- config storage (%APPDATA%\anvil\config.json) ----------
static std::string configPath() {
  char* base = nullptr; size_t sz = 0;
  std::string dir = ".";
  if (_dupenv_s(&base, &sz, "APPDATA") == 0 && base) { dir = std::string(base) + "\\anvil"; free(base); }
  CreateDirectoryA(dir.c_str(), nullptr);
  return dir + "\\config.json";
}
static json loadConfig() {
  std::ifstream f(configPath());
  if (!f) return json::object();
  try { json j; f >> j; return j; } catch (...) { return json::object(); }
}
static void saveConfig(const json& cfg) {
  std::ofstream f(configPath());
  if (f) f << cfg.dump(2);
}

// ---------- native tools ----------
static std::string readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return std::string();
  std::stringstream ss; ss << f.rdbuf(); return ss.str();
}
static bool writeFile(const std::string& path, const std::string& content) {
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f << content; return true;
}
static json listDir(const std::string& path) {
  json arr = json::array();
  std::string spec = path + "\\*";
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(spec.c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return arr;
  do {
    std::string name = fd.cFileName;
    if (name == "." || name == "..") continue;
    bool dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    arr.push_back({{"name", name}, {"path", path + "\\" + name}, {"dir", dir}});
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  return arr;
}
static std::string runCommand(const std::string& cmd, const std::string& cwd) {
  std::string full = cmd + " 2>&1";
  if (!cwd.empty()) full = "cd /d \"" + cwd + "\" && " + full;
  std::array<char, 256> buf{};
  std::string out;
  FILE* pipe = _popen(full.c_str(), "r");
  if (!pipe) return "ERROR: failed to start command";
  while (fgets(buf.data(), (int)buf.size(), pipe)) out += buf.data();
  _pclose(pipe);
  if (out.size() > 12000) out = out.substr(0, 12000) + "\n...[truncated]";
  return out;
}
static std::string pickFolder() {
  std::string result;
  ComPtr<IFileOpenDialog> dlg;
  if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg)))) return result;
  DWORD opts = 0; dlg->GetOptions(&opts); dlg->SetOptions(opts | FOS_PICKFOLDERS);
  if (FAILED(dlg->Show(g_hWnd))) return result;
  ComPtr<IShellItem> item;
  if (SUCCEEDED(dlg->GetResult(&item))) {
    PWSTR path = nullptr;
    if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) { result = narrow(path); CoTaskMemFree(path); }
  }
  return result;
}

// ---------- message bridge ----------
static void handleMessage(const std::string& text) {
  json req = json::parse(text, nullptr, false);
  if (req.is_discarded() || !req.contains("id")) return;
  json resp; resp["id"] = req["id"];
  std::string type = req.value("type", "");

  if (type == "loadConfig")       resp["cfg"] = loadConfig();
  else if (type == "saveConfig")  { saveConfig(req.value("cfg", json::object())); resp["ok"] = true; }
  else if (type == "openFolder")  resp["path"] = pickFolder();
  else if (type == "list")        resp["entries"] = listDir(req.value("path", std::string(".")));
  else if (type == "read")        resp["content"] = readFile(req.value("path", std::string()));
  else if (type == "write")       resp["ok"] = writeFile(req.value("path", std::string()), req.value("content", std::string()));
  else if (type == "run")         resp["output"] = runCommand(req.value("command", std::string()), req.value("cwd", std::string()));
  else                            resp["error"] = "unknown type";

  if (g_webview) g_webview->PostWebMessageAsString(widen(resp.dump()).c_str());
}

// ---------- window proc ----------
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_SIZE:
      if (g_controller) { RECT b; GetClientRect(hWnd, &b); g_controller->put_Bounds(b); }
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProc(hWnd, msg, wp, lp);
}

// ---------- webview boot ----------
static void loadUI() {
#if ANVIL_DEV
  ComPtr<ICoreWebView2_3> wv3;
  if (SUCCEEDED(g_webview.As(&wv3))) {
    wv3->SetVirtualHostNameToFolderMapping(L"anvil.local", uiFolder().c_str(),
        COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
    g_webview->Navigate(L"https://anvil.local/index.html");
    return;
  }
#endif
  g_webview->NavigateToString(widen(kAppHtml).c_str());
}

static void initWebView() {
  CreateCoreWebView2EnvironmentWithOptions(nullptr, nullptr, nullptr,
    Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
      [](HRESULT, ICoreWebView2Environment* env) -> HRESULT {
        env->CreateCoreWebView2Controller(g_hWnd,
          Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
            [](HRESULT, ICoreWebView2Controller* controller) -> HRESULT {
              if (!controller) return S_OK;
              g_controller = controller;
              g_controller->get_CoreWebView2(&g_webview);

              RECT b; GetClientRect(g_hWnd, &b);
              g_controller->put_Bounds(b);

              EventRegistrationToken tok;
              g_webview->add_WebMessageReceived(
                Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                  [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                    LPWSTR raw = nullptr;
                    if (SUCCEEDED(args->TryGetWebMessageAsString(&raw)) && raw) {
                      handleMessage(narrow(raw));
                      CoTaskMemFree(raw);
                    }
                    return S_OK;
                  }).Get(), &tok);

              // F5 reloads the UI (handy in dev mode)
              EventRegistrationToken acc;
              g_controller->add_AcceleratorKeyPressed(
                Callback<ICoreWebView2AcceleratorKeyPressedEventHandler>(
                  [](ICoreWebView2Controller*, ICoreWebView2AcceleratorKeyPressedEventArgs* args) -> HRESULT {
                    COREWEBVIEW2_KEY_EVENT_KIND kind; args->get_KeyEventKind(&kind);
                    UINT key = 0; args->get_VirtualKey(&key);
                    if (kind == COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN && key == VK_F5 && g_webview)
                      g_webview->Reload();
                    return S_OK;
                  }).Get(), &acc);

              loadUI();
              return S_OK;
            }).Get());
        return S_OK;
      }).Get());
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmd) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  WNDCLASSA wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hInst;
  wc.lpszClassName = "AnvilWindow";
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
  RegisterClassA(&wc);

  g_hWnd = CreateWindowExA(0, "AnvilWindow", "Anvil", WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, CW_USEDEFAULT, 1200, 760, nullptr, nullptr, hInst, nullptr);
  ShowWindow(g_hWnd, nCmd);
  UpdateWindow(g_hWnd);

  initWebView();

  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  CoUninitialize();
  return 0;
}
