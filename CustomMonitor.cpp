#include <windows.h>
#include <winhttp.h>
#include <cstdlib>
#include <string>
#include <fstream>
#include <atomic>
#include <chrono>
#include <cmath>
#include <process.h>
#include <vector>
#include <algorithm>
#include "PluginInterface.h"

#pragma comment(lib, "winhttp.lib")

// ================= 全局 =================
std::vector<std::wstring> g_configs;          // 仅存储URL
std::vector<std::wstring> g_responses;         // 存储URL返回的纯文本内容
std::atomic<int> g_current_index(0);
std::atomic<bool> g_updating(false);
std::chrono::steady_clock::time_point g_last_success;

// ================= INI 配置（仅URL，每行一个） =================
void LoadConfig()
{
    g_configs.clear();
    g_responses.clear();
    
    // 获取DLL路径
    HMODULE hModule = NULL;
    GetModuleHandleEx(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCTSTR)&LoadConfig,
        &hModule
    );
    
    wchar_t dllPath[MAX_PATH] = {0};
    GetModuleFileNameW(hModule, dllPath, MAX_PATH);
    std::wstring dllDir(dllPath);
    size_t pos = dllDir.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        dllDir = dllDir.substr(0, pos + 1);
    } else {
        dllDir = L".\\";
    }

    std::wstring iniPath = dllDir + L"custom_info.ini";
    std::wifstream f(iniPath.c_str());
    if (!f) {
        return; // 文件不存在，直接忽略
    }
    
    std::wstring line;
    while (std::getline(f, line)) {
        // 去除行首尾空白（包括全角空格、半角空格、制表符）
        size_t start = line.find_first_not_of(L" \t　");
        if (start == std::wstring::npos) continue;
        size_t end = line.find_last_not_of(L" \t　");
        line = line.substr(start, end - start + 1);
        
        // 验证是否是有效的URL（不符合的直接忽略）
        URL_COMPONENTS uc = { sizeof(uc) };
        wchar_t host[128] = {};
        wchar_t path[256] = {};
        uc.lpszHostName = host;
        uc.dwHostNameLength = 127;
        uc.lpszUrlPath = path;
        uc.dwUrlPathLength = 255;
        
        if (WinHttpCrackUrl(line.c_str(), 0, 0, &uc)) {
            g_configs.push_back(line);
            g_responses.push_back(L""); // 初始为空
        }
    }
}

// ================= HTTP（原样返回响应内容） =================
unsigned __stdcall HttpThreadProc(void* param)
{
    int index = reinterpret_cast<intptr_t>(param);
    
    if (index < 0 || index >= static_cast<int>(g_configs.size())) {
        g_updating = false;
        return 0;
    }
    
    const std::wstring& url = g_configs[index];
    
    try {
        HINTERNET h = WinHttpOpen(L"TrafficMonitor-PVE/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
        if (!h) {
            g_responses[index] = L"连接失败";
            g_updating = false;
            return 0;
        }

        int timeout = 3000;
        WinHttpSetTimeouts(h, timeout, timeout, timeout, timeout);

        URL_COMPONENTS uc = { sizeof(uc) };
        wchar_t host[128] = {};
        wchar_t path[256] = {};
        uc.lpszHostName = host;
        uc.dwHostNameLength = 127;
        uc.lpszUrlPath = path;
        uc.dwUrlPathLength = 255;

        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
            WinHttpCloseHandle(h);
            g_responses[index] = L"URL无效";
            g_updating = false;
            return 0;
        }

        HINTERNET c = WinHttpConnect(h, host, uc.nPort, 0);
        if (!c) {
            WinHttpCloseHandle(h);
            g_responses[index] = L"连接失败";
            g_updating = false;
            return 0;
        }

        HINTERNET r = WinHttpOpenRequest(c, L"GET", path, NULL, NULL, NULL, 0);
        if (!r) {
            WinHttpCloseHandle(c);
            WinHttpCloseHandle(h);
            g_responses[index] = L"请求失败";
            g_updating = false;
            return 0;
        }

        if (!WinHttpSendRequest(r, NULL, 0, NULL, 0, 0, 0)) {
            WinHttpCloseHandle(r);
            WinHttpCloseHandle(c);
            WinHttpCloseHandle(h);
            g_responses[index] = L"发送请求失败";
            g_updating = false;
            return 0;
        }

        if (!WinHttpReceiveResponse(r, NULL)) {
            WinHttpCloseHandle(r);
            WinHttpCloseHandle(c);
            WinHttpCloseHandle(h);
            g_responses[index] = L"接收响应失败";
            g_updating = false;
            return 0;
        }

        DWORD statusCode = 0;
        DWORD size = sizeof(statusCode);
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                           NULL, &statusCode, &size, NULL);

        if (statusCode == 200) {
			std::string response;
			char buf[1024] = {};
			DWORD read = 0;
			bool header_removed = false;

			while (WinHttpReadData(r, buf, sizeof(buf) - 1, &read) && read > 0) {
				buf[read] = '\0';
				response.append(buf);
			}

			// ✅ 关键：在整个 response 中去除 HTTP 头
			size_t header_end = response.find("\r\n\r\n");
			if (header_end != std::string::npos) {
				response = response.substr(header_end + 4);
			} else {
				header_end = response.find("\n\n");
				if (header_end != std::string::npos) {
					response = response.substr(header_end + 2);
				}
			}

			// ✅ 调试：打印原始内容到文件（临时用）
			// std::ofstream debug("debug_raw.txt", std::ios::binary);
			//debug << "=== RAW RESPONSE ===\n";
			//debug << response.substr(0, 200); // 只打印前200字符
			//debug.close();

			// ✅ 转为宽字符
			int wideLen = MultiByteToWideChar(CP_UTF8, 0, response.c_str(), -1, NULL, 0);
			if (wideLen > 0) {
				std::wstring wideResponse(wideLen, L'\0');
				MultiByteToWideChar(CP_UTF8, 0, response.c_str(), -1, &wideResponse[0], wideLen);
				g_responses[index] = wideResponse;
			} else {
				g_responses[index] = L"内容解码失败";
			}

			g_last_success = std::chrono::steady_clock::now();
		} else {
            g_responses[index] = L"HTTP错误: " + std::to_wstring(statusCode);
        }

        WinHttpCloseHandle(r);
        WinHttpCloseHandle(c);
        WinHttpCloseHandle(h);
    }
    catch (...) {
        g_responses[index] = L"请求异常";
    }
    
    g_updating = false;
    return 0;
}

// ================= Item =================
class CPveTempItem : public IPluginItem {
public:
    const wchar_t* GetItemName() const override { return L"PVE监控"; }
    const wchar_t* GetItemId() const override { return L"Pve001"; }
    const wchar_t* GetItemLableText() const override { return L""; }
    const wchar_t* GetItemValueText() const override { return L""; }
    const wchar_t* GetItemValueSampleText() const override { return L""; }
};

// ================= Plugin =================
class CPveTempPlugin : public ITMPlugin {
    CPveTempItem item;
public:
    CPveTempPlugin() {
        LoadConfig();
        g_last_success = std::chrono::steady_clock::now() - std::chrono::hours(1);
    }

    const wchar_t* GetInfo(PluginInfoIndex index) override {
        switch (index) {
        case TMI_NAME:        return L"自定义监控";
        case TMI_DESCRIPTION: return L"通过HTTP获取服务器返回内容";
        case TMI_AUTHOR:      return L"zqq12345";
        case TMI_COPYRIGHT:   return L"Copyright © 2026";
        case TMI_VERSION:     return L"1.0.0";
        case TMI_URL:         return L"https://github.com/zqq12345/custom-info-monitor-plugin";
        default: return L"";
        }
    }

    IPluginItem* GetItem(int i) override {
        return (i == 0) ? &item : nullptr;
    }

    void DataRequired() override {
        if (g_configs.empty()) return;
        if (g_updating) return;
        
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - g_last_success).count() < 5) {
            return;
        }
        
        int index = g_current_index.fetch_add(1) % g_configs.size();
        
        uintptr_t th = _beginthreadex(
            NULL,
            0,
            HttpThreadProc,
            reinterpret_cast<void*>(index),
            0,
            NULL
        );
        
        if (th == 0) {
            g_updating = false;
        } else {
            CloseHandle((HANDLE)th);
        }
    }

    OptionReturn ShowOptionsDialog(void* parent) override {
        std::wstring message = L"自定义监控插件设置\n";
		message += L"请确保插件目录下存在 custom_info.ini 文件\n";
		message += L"格式：每行一个URL（不符合URL格式的会自动忽略）\n";
		message += L"示例：\n";
		message += L"http://192.168.3.10:18080/sensors\n";
		message += L"如需修改，请编辑 custom_info.ini 文件\n";
		message += L"修改后重启 TrafficMonitor 生效";
        MessageBoxW((HWND)parent, message.c_str(), L"自定义监控插件", MB_OK);
        return OR_OPTION_CHANGED;
    }

	const wchar_t* GetTooltipInfo() override {
		static wchar_t tip[4096];
		
		if (g_configs.empty()) {
			//swprintf(tip, 4096, L"❌ 未配置URL\n请创建 custom_info.ini");
			return tip;
		}
		
		int index = g_current_index.load() % g_configs.size();
		const std::wstring& url = g_configs[index];
		const std::wstring& content = g_responses[index];
		
		if (content.empty()) {
			swprintf(tip, 4096, L"%ls\n\n未获取数据", url.c_str());
		} else {
			// ✅ 修复：用 %s 而不是 %ls
			swprintf(tip, 4096, L"\n%ls",content.c_str());
		}
		return tip;
	}
};

CPveTempPlugin g_plugin;

extern "C" __declspec(dllexport)
ITMPlugin* TMPluginGetInstance() {
    return &g_plugin;
}