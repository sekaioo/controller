module;
#include <comutil.h>
#include <netfw.h>

#include <mutex>
#include <sstream>

#include "constants.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

export module components.NetworkBlocker;

import components.Config;
import components.I18n;

using namespace std;

class ComInit {
public:
    // @formatter:off
    ComInit() :
        hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComInit() { if(SUCCEEDED(hr)) CoUninitialize(); }

    [[nodiscard]] bool is_ok() const { return SUCCEEDED(hr); }

    // 禁用拷贝和移动操作
    ComInit(const ComInit&) = delete;
    ComInit& operator=(const ComInit&) = delete;
    ComInit(ComInit&&) = delete;
    ComInit& operator=(ComInit&&) = delete;
    //@formatter:on

private:
    HRESULT hr;
};

template<typename T>
class ComPtr {
public:
    // @formatter:off
    ComPtr() = default;
    ~ComPtr() { if(p) p->Release(); }

    T* operator->() const { return p; }
    [[nodiscard]] T* get() const { return p; }
    T** release_and_get_address() {
        if(p) {
            p->Release();
            p = nullptr;
        }
        return &p;
    }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ComPtr(ComPtr&&) = delete;
    ComPtr& operator=(ComPtr&&) = delete;
    // @formatter:on

private:
    T* p = nullptr;
};

// @formatter:off
export class NetworkBlocker {
public:
    static NetworkBlocker& instance();
    static void initialize(bool enable_block_network) ;
    void block_network() const;
    void unblock_network() const;
    NetworkBlocker(const NetworkBlocker&) = delete;
    NetworkBlocker& operator=(const NetworkBlocker&) = delete;
    NetworkBlocker(NetworkBlocker&&) = delete;
    NetworkBlocker& operator=(NetworkBlocker&&) = delete;
private:
    NetworkBlocker() {
        remove_rule();
    }
    ~NetworkBlocker() { remove_rule(); }
    static bool add_rule();
    static bool remove_rule();
    inline static bool enable_block_network_;
    mutable mutex mutex_;
};
// @formatter:on

NetworkBlocker& NetworkBlocker::instance() {
    static NetworkBlocker instance;
    return instance;
}

void NetworkBlocker::initialize(bool enable_block_network) {
    enable_block_network_ = enable_block_network;
}

void NetworkBlocker::block_network() const {
    lock_guard lock(mutex_);
    if(!enable_block_network_) return;
    add_rule();
}

void NetworkBlocker::unblock_network() const {
    lock_guard lock(mutex_);
    remove_rule();
}

bool NetworkBlocker::add_rule() {
    const ComInit com_init;
    if(!com_init.is_ok()) return false;

    HRESULT hr = S_OK;
    ComPtr<INetFwPolicy2> pNetFwPolicy2;
    ComPtr<INetFwRules> pNetFwRules;
    ComPtr<INetFwRule> pNetFwRule;

    // 创建防火墙策略实例
    hr = CoCreateInstance(
        __uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(INetFwPolicy2),
        reinterpret_cast<void**>(pNetFwPolicy2.release_and_get_address()));
    if(FAILED(hr)) return false;

    // 获取规则集合
    hr = pNetFwPolicy2->get_Rules(pNetFwRules.release_and_get_address());
    if(FAILED(hr)) return false;

    // 创建新规则实例
    hr = CoCreateInstance(
        __uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER, __uuidof(INetFwRule),
        reinterpret_cast<void**>(pNetFwRule.release_and_get_address()));
    if(FAILED(hr)) return false;

    // 填充规则属性
    const std::wstring desc = wtr("dialog.network_blocked");
    pNetFwRule->put_Name(_bstr_t(FIREWALL_RULE_NAME));
    pNetFwRule->put_Description(_bstr_t(desc.c_str()));
    pNetFwRule->put_Action(NET_FW_ACTION_BLOCK);
    pNetFwRule->put_Direction(NET_FW_RULE_DIR_OUT);
    pNetFwRule->put_Enabled(VARIANT_TRUE);

    // 添加规则
    hr = pNetFwRules->Add(pNetFwRule.get());
    return SUCCEEDED(hr);
}

bool NetworkBlocker::remove_rule() {
    const ComInit com_init;
    if(!com_init.is_ok()) return false;

    HRESULT hr = S_OK;
    ComPtr<INetFwPolicy2> pNetFwPolicy2;
    ComPtr<INetFwRules> pNetFwRules;

    // 创建防火墙策略实例
    hr = CoCreateInstance(
        __uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER,
        __uuidof(INetFwPolicy2),
        reinterpret_cast<void**>(pNetFwPolicy2.release_and_get_address()));
    if(FAILED(hr)) return false;

    // 获取规则集合
    hr = pNetFwPolicy2->get_Rules(pNetFwRules.release_and_get_address());
    if(FAILED(hr)) return false;

    // 移除规则
    hr = pNetFwRules->Remove(_bstr_t(FIREWALL_RULE_NAME));
    return SUCCEEDED(hr);
}
