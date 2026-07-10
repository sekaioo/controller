module;
#include <comutil.h>
#include <netfw.h>

#include <mutex>

#include "constants.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

export module components.NetworkBlocker;

import components.Config;
import components.I18n;

using namespace std;

// @formatter:off
class ComInit {
public:
    ComInit() :
        hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComInit() { if(SUCCEEDED(hr)) CoUninitialize(); }

    [[nodiscard]] bool is_ok() const { return SUCCEEDED(hr); }
    ComInit(const ComInit&) = delete;
    ComInit& operator=(const ComInit&) = delete;
    ComInit(ComInit&&) = delete;
    ComInit& operator=(ComInit&&) = delete;
private:
    HRESULT hr;
};
//@formatter:on

// @formatter:off
template<typename T>
class ComPtr {
public:
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
private:
    T* p = nullptr;
};
// @formatter:on

// 模块内部状态: 配置开关与串行化多线程调用的互斥锁
static bool enable_block_network_ = false;
static mutex block_mutex_;

static bool add_rule() {
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

    // 同名规则已存在时直接复用, 避免重复添加导致规则累积
    hr = pNetFwRules->Item(_bstr_t(FIREWALL_RULE_NAME),
                           pNetFwRule.release_and_get_address());
    if(SUCCEEDED(hr)) return true;

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

static bool remove_rule() {
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

    // 同名规则可能存在多条(历史版本残留), 循环删除直到不存在, 上限防止意外死循环
    for(int i = 0; i < 16; ++i) {
        hr = pNetFwRules->Item(_bstr_t(FIREWALL_RULE_NAME),
                               pNetFwRule.release_and_get_address());
        if(FAILED(hr)) break;
        if(FAILED(pNetFwRules->Remove(_bstr_t(FIREWALL_RULE_NAME)))) return false;
    }
    return true;
}

export namespace network_blocker {
    // 记录配置开关, block() 仅在开关打开时生效
    void initialize(const bool enable_block_network) {
        enable_block_network_ = enable_block_network;
    }

    void block() {
        lock_guard lock(block_mutex_);
        if(!enable_block_network_) return;
        add_rule();
    }

    void unblock() {
        lock_guard lock(block_mutex_);
        remove_rule();
    }
}
