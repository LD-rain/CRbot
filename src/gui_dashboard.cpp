#include "gui_dashboard.h"
#include "crbot_version.h"
#include <commctrl.h>

#ifndef CRBOT_VERSION_W
#define CRBOT_VERSION_W L"dev"
#endif

#pragma comment(lib, "comctl32.lib")

static constexpr int TIMER_ID = 1;
static constexpr int BTN_WAIT_BASE = 1000;
static constexpr int BTN_DISC_BASE  = 2000;
static constexpr int BTN_RETRY_BASE = 3000;
static constexpr int BTN_STOP_BASE  = 4000;

// 请求类型，和 DeviceState::request 对应
static constexpr int REQ_RESTART = 1;
static constexpr int REQ_ABORT = 2;

GuiDashboard::GuiDashboard(HINSTANCE hInstance, std::vector<DeviceState*>& states)
    : hInstance_(hInstance), states_(states) {

    WNDCLASSEX wc = {};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CRBotDashboard";
    RegisterClassEx(&wc);

    int height = 40 + static_cast<int>(states_.size()) * 75;
    hwnd_ = CreateWindowEx(0, L"CRBotDashboard",
                           L"CRBot " CRBOT_VERSION_W L" - Device Manager",
                           WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                           CW_USEDEFAULT, CW_USEDEFAULT, width_, height,
                           nullptr, nullptr, hInstance, this);

    create_controls(hwnd_);
    SetTimer(hwnd_, TIMER_ID, 500, nullptr);
}

GuiDashboard::~GuiDashboard() {
    if (hwnd_) {
        KillTimer(hwnd_, TIMER_ID);
        DestroyWindow(hwnd_);
    }
}

void GuiDashboard::run() {
    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

void GuiDashboard::create_controls(HWND parent) {
    int n = static_cast<int>(states_.size());
    lbl_status_.resize(n);
    btn_wait_.resize(n);
    btn_disconnect_.resize(n);
    btn_retry_.resize(n);
    btn_stop_.resize(n);

    for (int i = 0; i < n; ++i) {
        int y = 10 + i * 75;

        // 设备序列号标签
        std::wstring wserial(states_[i]->serial.begin(), states_[i]->serial.end());
        CreateWindowEx(0, L"STATIC", wserial.c_str(),
                       WS_CHILD | WS_VISIBLE | SS_LEFT,
                       10, y, 580, 18, parent, nullptr, hInstance_, nullptr);

        // 状态标签
        lbl_status_[i] = CreateWindowEx(0, L"STATIC", L"...",
                        WS_CHILD | WS_VISIBLE | SS_LEFT,
                        10, y + 20, 580, 18, parent, nullptr, hInstance_, nullptr);

        // 按钮（初始隐藏）
        btn_wait_[i] = CreateWindowEx(0, L"BUTTON", L"Wait",
                        WS_CHILD | BS_PUSHBUTTON,
                        150, y + 42, 70, 24, parent,
                        (HMENU)(INT_PTR)(BTN_WAIT_BASE + i), hInstance_, nullptr);

        btn_disconnect_[i] = CreateWindowEx(0, L"BUTTON", L"Disconnect",
                        WS_CHILD | BS_PUSHBUTTON,
                        225, y + 42, 95, 24, parent,
                        (HMENU)(INT_PTR)(BTN_DISC_BASE + i), hInstance_, nullptr);

        // 打断：立刻停掉这台设备，不等 adb 超时
        btn_stop_[i] = CreateWindowEx(0, L"BUTTON", L"Stop",
                        WS_CHILD | BS_PUSHBUTTON,
                        325, y + 42, 70, 24, parent,
                        (HMENU)(INT_PTR)(BTN_STOP_BASE + i), hInstance_, nullptr);

        // 重试：人工处理完之后，从"连设备"那一步整台重跑
        btn_retry_[i] = CreateWindowEx(0, L"BUTTON", L"Retry",
                        WS_CHILD | BS_PUSHBUTTON,
                        400, y + 42, 70, 24, parent,
                        (HMENU)(INT_PTR)(BTN_RETRY_BASE + i), hInstance_, nullptr);
    }
}

void GuiDashboard::refresh() {
    for (size_t i = 0; i < states_.size(); ++i) {
        std::string status;
        bool awaiting = false;
        bool error = false;
        {
            std::lock_guard<std::mutex> lock(states_[i]->mtx);
            status = states_[i]->status;
            awaiting = states_[i]->awaiting_input;
            error = states_[i]->error;
        }

        std::wstring wstatus(status.begin(), status.end());
        if (error) wstatus = L"!! " + wstatus;
        SetWindowText(lbl_status_[i], wstatus.c_str());

        const int show_wait = awaiting ? SW_SHOW : SW_HIDE;
        ShowWindow(btn_wait_[i], show_wait);
        ShowWindow(btn_disconnect_[i], show_wait);

        // Retry / Stop 一直可点：
        //   Retry —— 玩家手动处理完（关弹窗、重新登录……）之后，整台从"连设备"重跑
        //   Stop  —— 立刻打断（连正在执行的 adb 命令一起杀掉），不等超时
        ShowWindow(btn_retry_[i], SW_SHOW);
        ShowWindow(btn_stop_[i], SW_SHOW);
    }
}

void GuiDashboard::on_wait(int idx) {
    std::lock_guard<std::mutex> lock(states_[idx]->mtx);
    states_[idx]->user_response = 1;
    states_[idx]->cv.notify_all();
}

void GuiDashboard::on_disconnect(int idx) {
    std::lock_guard<std::mutex> lock(states_[idx]->mtx);
    states_[idx]->user_response = 2;
    states_[idx]->cv.notify_all();
}

// 只登记请求，真正的 stop/join/start 由 main 的监视线程做——
// 界面线程绝对不能阻塞在 join() 上。
void GuiDashboard::post_request(int idx, int request) {
    std::lock_guard<std::mutex> lock(states_[idx]->mtx);
    states_[idx]->request = request;
}

void GuiDashboard::on_retry(int idx) { post_request(idx, REQ_RESTART); }

void GuiDashboard::on_abort(int idx) { post_request(idx, REQ_ABORT); }

LRESULT CALLBACK GuiDashboard::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    GuiDashboard* self = nullptr;

    if (msg == WM_CREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = static_cast<GuiDashboard*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        return 0;
    }

    self = reinterpret_cast<GuiDashboard*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    if (!self) return DefWindowProc(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_TIMER:
        if (wp == TIMER_ID) self->refresh();
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= BTN_WAIT_BASE && id < BTN_WAIT_BASE + 100) {
            self->on_wait(id - BTN_WAIT_BASE);
        } else if (id >= BTN_DISC_BASE && id < BTN_DISC_BASE + 100) {
            self->on_disconnect(id - BTN_DISC_BASE);
        } else if (id >= BTN_RETRY_BASE && id < BTN_RETRY_BASE + 100) {
            self->on_retry(id - BTN_RETRY_BASE);
        } else if (id >= BTN_STOP_BASE && id < BTN_STOP_BASE + 100) {
            self->on_abort(id - BTN_STOP_BASE);
        }
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}
