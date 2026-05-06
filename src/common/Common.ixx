module;
#include <Windows.h>
#include <functional>
export module common.Common;

// 适用于 windows 的程序互斥锁
export class MutexGuard {
public:
    explicit MutexGuard(const wchar_t* name) {
        handle_ = CreateMutexW(nullptr, TRUE, name);
        if(handle_ != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
            CloseHandle(handle_);
            handle_ = nullptr;
        }
    }

    ~MutexGuard() {
        if(handle_) {
            ReleaseMutex(handle_);
            CloseHandle(handle_);
        }
    }

    [[nodiscard]] bool is_run() const { return handle_ != nullptr; }

    // @formatter:off
    MutexGuard(const MutexGuard&) = delete;
    MutexGuard& operator=(const MutexGuard&) = delete;
    // @formatter:on

private:
    HANDLE handle_;
};

// @formatter:off
export struct HandleGuard {
    HANDLE h;
    explicit HandleGuard(HANDLE h) : h(h) {}
    ~HandleGuard() { if(h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    HandleGuard(const HandleGuard&) = delete;
    HandleGuard& operator=(const HandleGuard&) = delete;
};

export struct ScopeGuard {
    std::function<void()> fn;
    bool active = true;
    explicit ScopeGuard(std::function<void()> f) : fn(move(f)) {}
    ~ScopeGuard() { if(active) fn(); }
    void dismiss() { active = false; }
};
//@formatter:on
