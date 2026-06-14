// Anvil — a BYOK agentic coding assistant for your terminal.
// Default provider: OpenAI-compatible chat completions. Optional: Anthropic (Claude).
// (c) 2026 Abhyudaya Mishra. All rights reserved. Proprietary — see LICENSE.
//
// Windows build: uses WinHTTP (no external HTTP dependency) + nlohmann/json.

#include <windows.h>
#include <winhttp.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

using json = nlohmann::json;

// ---------- terminal colors ----------
namespace clr {
  const char* reset = "\x1b[0m";
  const char* dim   = "\x1b[2m";
  const char* bold  = "\x1b[1m";
  const char* cyan  = "\x1b[36m";
  const char* green = "\x1b[32m";
  const char* yellow= "\x1b[33m";
  const char* red   = "\x1b[31m";
  const char* grey  = "\x1b[90m";
}

static void enableVT() {
  HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD mode = 0;
  if (GetConsoleMode(h, &mode)) SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  SetConsoleOutputCP(CP_UTF8);
}

// ---------- small utils ----------
static std::wstring widen(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
  std::wstring w(n, 0);
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
  return w;
}

static std::string getEnv(const char* key) {
  char* buf = nullptr; size_t sz = 0;
  if (_dupenv_s(&buf, &sz, key) == 0 && buf) { std::string v(buf); free(buf); return v; }
  return "";
}

struct Url { std::wstring host; std::wstring path; };
static Url parseUrl(const std::string& full) {
  std::string s = full;
  auto pos = s.find("://");
  if (pos != std::string::npos) s = s.substr(pos + 3);
  auto slash = s.find('/');
  std::string host = (slash == std::string::npos) ? s : s.substr(0, slash);
  std::string path = (slash == std::string::npos) ? "/" : s.substr(slash);
  return { widen(host), widen(path) };
}

// ---------- HTTP (WinHTTP, HTTPS POST) ----------
static std::string httpsPost(const std::string& fullUrl,
                             const std::vector<std::string>& headers,
                             const std::string& body, long& statusOut) {
  statusOut = 0;
  Url u = parseUrl(fullUrl);
  std::string out;

  HINTERNET hSession = WinHttpOpen(L"Anvil/1.0",
      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!hSession) return out;

  HINTERNET hConnect = WinHttpConnect(hSession, u.host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
  if (!hConnect) { WinHttpCloseHandle(hSession); return out; }

  HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"POST", u.path.c_str(), nullptr,
      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
  if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return out; }

  std::wstring hdr;
  for (const auto& h : headers) hdr += widen(h) + L"\r\n";

  BOOL ok = WinHttpSendRequest(hRequest, hdr.c_str(), (DWORD)-1L,
      (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);
  if (ok) ok = WinHttpReceiveResponse(hRequest, nullptr);

  if (ok) {
    DWORD code = 0, len = sizeof(code);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
    statusOut = (long)code;

    DWORD avail = 0;
    do {
      avail = 0;
      if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0) break;
      std::string chunk(avail, 0);
      DWORD read = 0;
      if (WinHttpReadData(hRequest, &chunk[0], avail, &read) && read > 0) {
        chunk.resize(read);
        out += chunk;
      }
    } while (avail > 0);
  }

  WinHttpCloseHandle(hRequest);
  WinHttpCloseHandle(hConnect);
  WinHttpCloseHandle(hSession);
  return out;
}

// ---------- config ----------
struct Config {
  std::string provider = "openai";          // "openai" | "anthropic"
  std::string apiKey;
  std::string baseUrl;                       // host root, no trailing slash
  std::string model;
};

static Config loadConfig() {
  Config c;
  // try config file: %USERPROFILE%\.anvil\config.json
  std::string home = getEnv("USERPROFILE");
  if (!home.empty()) {
    std::ifstream f(home + "\\.anvil\\config.json");
    if (f) {
      try {
        json j; f >> j;
        if (j.contains("provider")) c.provider = j["provider"].get<std::string>();
        if (j.contains("apiKey"))   c.apiKey   = j["apiKey"].get<std::string>();
        if (j.contains("baseUrl"))  c.baseUrl  = j["baseUrl"].get<std::string>();
        if (j.contains("model"))    c.model    = j["model"].get<std::string>();
      } catch (...) {}
    }
  }
  // env overrides
  if (auto v = getEnv("ANVIL_PROVIDER"); !v.empty()) c.provider = v;
  if (auto v = getEnv("ANVIL_API_KEY");  !v.empty()) c.apiKey   = v;
  if (auto v = getEnv("ANVIL_BASE_URL"); !v.empty()) c.baseUrl  = v;
  if (auto v = getEnv("ANVIL_MODEL");    !v.empty()) c.model    = v;
  // also accept the conventional OPENAI/ANTHROPIC keys
  if (c.apiKey.empty() && c.provider == "openai")    c.apiKey = getEnv("OPENAI_API_KEY");
  if (c.apiKey.empty() && c.provider == "anthropic") c.apiKey = getEnv("ANTHROPIC_API_KEY");

  if (c.baseUrl.empty())
    c.baseUrl = (c.provider == "anthropic") ? "https://api.anthropic.com" : "https://api.openai.com";
  if (c.model.empty())
    c.model = (c.provider == "anthropic") ? "claude-3-5-sonnet-latest" : "gpt-4o-mini";
  return c;
}

// ---------- tools ----------
static std::string readFileTool(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return "ERROR: could not open file: " + path;
  std::stringstream ss; ss << f.rdbuf();
  return ss.str();
}

static bool confirm(const std::string& what) {
  std::cout << clr::yellow << "  ? " << what << " [y/N]: " << clr::reset;
  std::string line; std::getline(std::cin, line);
  return !line.empty() && (line[0] == 'y' || line[0] == 'Y');
}

static std::string writeFileTool(const std::string& path, const std::string& content) {
  if (!confirm("Write " + std::to_string(content.size()) + " bytes to '" + path + "'?"))
    return "User declined the write.";
  std::ofstream f(path, std::ios::binary);
  if (!f) return "ERROR: could not write file: " + path;
  f << content;
  return "Wrote " + std::to_string(content.size()) + " bytes to " + path;
}

static std::string listDirTool(const std::string& path) {
  std::string p = path.empty() ? "." : path;
  std::string spec = p + "\\*";
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(spec.c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return "ERROR: cannot list: " + p;
  std::string out;
  do {
    std::string name = fd.cFileName;
    if (name == "." || name == "..") continue;
    bool dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    out += (dir ? "[dir]  " : "       ") + name + "\n";
  } while (FindNextFileA(h, &fd));
  FindClose(h);
  return out.empty() ? "(empty)" : out;
}

static std::string runCommandTool(const std::string& cmd) {
  if (!confirm("Run command: " + cmd + " ?")) return "User declined to run the command.";
  std::string full = cmd + " 2>&1";
  std::array<char, 256> buf{};
  std::string out;
  FILE* pipe = _popen(full.c_str(), "r");
  if (!pipe) return "ERROR: failed to start command.";
  while (fgets(buf.data(), (int)buf.size(), pipe)) out += buf.data();
  _pclose(pipe);
  if (out.size() > 8000) out = out.substr(0, 8000) + "\n...[truncated]";
  return out.empty() ? "(no output)" : out;
}

static std::string dispatchTool(const std::string& name, const json& args) {
  try {
    if (name == "read_file")   return readFileTool(args.at("path").get<std::string>());
    if (name == "write_file")  return writeFileTool(args.at("path").get<std::string>(), args.at("content").get<std::string>());
    if (name == "list_dir")    return listDirTool(args.value("path", std::string(".")));
    if (name == "run_command") return runCommandTool(args.at("command").get<std::string>());
  } catch (const std::exception& e) {
    return std::string("ERROR: bad arguments: ") + e.what();
  }
  return "ERROR: unknown tool: " + name;
}

static const char* SYSTEM_PROMPT =
  "You are Anvil, a precise AI coding assistant running in the user's terminal. "
  "You can read and write files, list directories, and run shell commands using the provided tools. "
  "Plan briefly, use tools to inspect the project before editing, make minimal correct changes, "
  "and explain what you did in a short final summary.";

// ---------- tool schemas ----------
static json openaiTools() {
  auto fn = [](const char* name, const char* desc, json props, json req) {
    return json{{"type","function"},{"function",{{"name",name},{"description",desc},
      {"parameters",{{"type","object"},{"properties",props},{"required",req}}}}}};
  };
  return json::array({
    fn("read_file","Read a UTF-8 text file and return its contents.",
       {{"path",{{"type","string"},{"description","File path"}}}}, json::array({"path"})),
    fn("write_file","Create or overwrite a file (asks the user for confirmation).",
       {{"path",{{"type","string"}}},{"content",{{"type","string"}}}}, json::array({"path","content"})),
    fn("list_dir","List the entries in a directory.",
       {{"path",{{"type","string"},{"description","Directory path, default '.'"}}}}, json::array()),
    fn("run_command","Run a shell command (asks the user for confirmation). Returns stdout+stderr.",
       {{"command",{{"type","string"}}}}, json::array({"command"})),
  });
}

static json anthropicTools() {
  auto t = [](const char* name, const char* desc, json props, json req) {
    return json{{"name",name},{"description",desc},
      {"input_schema",{{"type","object"},{"properties",props},{"required",req}}}};
  };
  return json::array({
    t("read_file","Read a UTF-8 text file and return its contents.",
      {{"path",{{"type","string"}}}}, json::array({"path"})),
    t("write_file","Create or overwrite a file (asks the user for confirmation).",
      {{"path",{{"type","string"}}},{"content",{{"type","string"}}}}, json::array({"path","content"})),
    t("list_dir","List the entries in a directory.",
      {{"path",{{"type","string"}}}}, json::array()),
    t("run_command","Run a shell command (asks the user for confirmation).",
      {{"command",{{"type","string"}}}}, json::array({"command"})),
  });
}

// ---------- OpenAI agent loop ----------
static void runOpenAI(const Config& cfg, json& messages) {
  for (int step = 0; step < 25; ++step) {
    json body = {{"model", cfg.model}, {"messages", messages},
                 {"tools", openaiTools()}, {"tool_choice", "auto"}};
    std::cout << clr::grey << "  · thinking…" << clr::reset << "\r";
    long status = 0;
    std::string resp = httpsPost(cfg.baseUrl + "/v1/chat/completions",
        {"Content-Type: application/json", "Authorization: Bearer " + cfg.apiKey},
        body.dump(), status);
    std::cout << "             \r";

    if (status != 200) {
      std::cout << clr::red << "  API error (" << status << "): " << clr::reset << resp.substr(0, 400) << "\n";
      return;
    }
    json j;
    try { j = json::parse(resp); } catch (...) { std::cout << clr::red << "  bad JSON from API\n" << clr::reset; return; }

    json msg = j["choices"][0]["message"];
    messages.push_back(msg);

    if (msg.contains("tool_calls") && msg["tool_calls"].is_array() && !msg["tool_calls"].empty()) {
      for (const auto& call : msg["tool_calls"]) {
        std::string name = call["function"]["name"].get<std::string>();
        json args;
        try { args = json::parse(call["function"]["arguments"].get<std::string>()); } catch (...) { args = json::object(); }
        std::cout << clr::cyan << "  → " << name << clr::reset << " " << clr::dim << args.dump() << clr::reset << "\n";
        std::string result = dispatchTool(name, args);
        messages.push_back({{"role","tool"},{"tool_call_id", call["id"]},{"content", result}});
      }
      continue;
    }

    if (msg.contains("content") && !msg["content"].is_null()) {
      std::cout << "\n" << clr::green << msg["content"].get<std::string>() << clr::reset << "\n\n";
    }
    return;
  }
  std::cout << clr::yellow << "  (stopped after 25 steps)\n" << clr::reset;
}

// ---------- Anthropic agent loop ----------
static void runAnthropic(const Config& cfg, json& messages) {
  for (int step = 0; step < 25; ++step) {
    json body = {{"model", cfg.model}, {"max_tokens", 4096}, {"system", SYSTEM_PROMPT},
                 {"messages", messages}, {"tools", anthropicTools()}};
    std::cout << clr::grey << "  · thinking…" << clr::reset << "\r";
    long status = 0;
    std::string resp = httpsPost(cfg.baseUrl + "/v1/messages",
        {"Content-Type: application/json", "x-api-key: " + cfg.apiKey, "anthropic-version: 2023-06-01"},
        body.dump(), status);
    std::cout << "             \r";

    if (status != 200) {
      std::cout << clr::red << "  API error (" << status << "): " << clr::reset << resp.substr(0, 400) << "\n";
      return;
    }
    json j;
    try { j = json::parse(resp); } catch (...) { std::cout << clr::red << "  bad JSON from API\n" << clr::reset; return; }

    json content = j["content"];
    messages.push_back({{"role","assistant"},{"content", content}});

    json toolResults = json::array();
    std::string finalText;
    for (const auto& block : content) {
      std::string type = block.value("type", "");
      if (type == "text") finalText += block.value("text", "");
      else if (type == "tool_use") {
        std::string name = block["name"].get<std::string>();
        json args = block.value("input", json::object());
        std::cout << clr::cyan << "  → " << name << clr::reset << " " << clr::dim << args.dump() << clr::reset << "\n";
        std::string result = dispatchTool(name, args);
        toolResults.push_back({{"type","tool_result"},{"tool_use_id", block["id"]},{"content", result}});
      }
    }

    if (!toolResults.empty()) {
      messages.push_back({{"role","user"},{"content", toolResults}});
      continue;
    }
    if (!finalText.empty()) std::cout << "\n" << clr::green << finalText << clr::reset << "\n\n";
    return;
  }
  std::cout << clr::yellow << "  (stopped after 25 steps)\n" << clr::reset;
}

static void banner(const Config& cfg) {
  std::cout << clr::cyan << clr::bold
            << "  ╔══════════════════════════════════╗\n"
            << "  ║   A N V I L  ·  coding agent     ║\n"
            << "  ╚══════════════════════════════════╝\n" << clr::reset;
  std::cout << clr::grey << "  provider: " << cfg.provider << "   model: " << cfg.model << "\n"
            << "  type a task, or 'exit' to quit. tools ask before writing/running.\n\n" << clr::reset;
}

int main(int argc, char** argv) {
  enableVT();
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--help" || a == "-h") {
      std::cout << "Anvil — BYOK agentic coding CLI\n"
                   "Config via env: ANVIL_PROVIDER(openai|anthropic), ANVIL_API_KEY, ANVIL_MODEL, ANVIL_BASE_URL\n"
                   "  or %USERPROFILE%\\.anvil\\config.json\n";
      return 0;
    }
  }

  Config cfg = loadConfig();
  banner(cfg);

  if (cfg.apiKey.empty()) {
    std::cout << clr::red << "  No API key found. Set ANVIL_API_KEY (or OPENAI_API_KEY / ANTHROPIC_API_KEY).\n" << clr::reset;
    return 1;
  }

  // conversation state per provider
  json messages = (cfg.provider == "anthropic") ? json::array() : json::array({{{"role","system"},{"content", SYSTEM_PROMPT}}});

  while (true) {
    std::cout << clr::bold << "anvil› " << clr::reset;
    std::string task;
    if (!std::getline(std::cin, task)) break;
    if (task == "exit" || task == "quit") break;
    if (task.empty()) continue;

    messages.push_back({{"role","user"},{"content", task}});
    if (cfg.provider == "anthropic") runAnthropic(cfg, messages);
    else runOpenAI(cfg, messages);
  }
  std::cout << clr::grey << "  bye.\n" << clr::reset;
  return 0;
}
