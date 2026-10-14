#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace dlog {

/**
 * @brief Windowsの自動スリープ・休止状態を抑制するRAIIガードクラス
 * 長時間ロギング中のOS省電力移行を防止します。
 */
class PowerManagementGuard {
public:
    PowerManagementGuard() : m_active(false) {}

    ~PowerManagementGuard() {
        Release();
    }

    void Acquire() {
#ifdef _WIN32
        if (!m_active) {
            // システムスリープおよびディスプレイオフ後の休止を防止
            EXECUTION_STATE state = SetThreadExecutionState(
                ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_AWAYMODE_REQUIRED
            );
            if (state == 0) {
                // AWAYMODEが非対応の場合はSYSTEM_REQUIREDのみで再試行
                SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED);
            }
            m_active = true;
        }
#endif
    }

    void Release() {
#ifdef _WIN32
        if (m_active) {
            SetThreadExecutionState(ES_CONTINUOUS);
            m_active = false;
        }
#endif
    }

    bool IsActive() const {
        return m_active;
    }

private:
    bool m_active = false;
};

} // namespace dlog
