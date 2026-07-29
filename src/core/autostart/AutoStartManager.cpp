#include "core/autostart/AutoStartManager.h"

#include "core/config/ConfigManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QString>

#ifdef _WIN32
#  ifndef WINVER
#    define WINVER 0x0A00
#  endif
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0A00
#  endif
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#endif

namespace {
#ifdef _WIN32
// HKCU\Software\Microsoft\Windows\CurrentVersion\Run 下 XTranslate 的注册值名。
// 不可与产品安装包的卸载项冲突，沿用可执行文件名以保持一致。
constexpr wchar_t kRunValueName[] = L"XTranslate";
constexpr wchar_t kRunKeyPath[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

// 构造命令行 `"<exe>" --minimized`，路径含空格时必须加引号。
QString buildRunCommand()
{
    const QString exe = QCoreApplication::applicationFilePath();
    return QStringLiteral("\"%1\" --minimized").arg(exe);
}
#endif
} // namespace

AutoStartManager &AutoStartManager::instance()
{
    static AutoStartManager mgr;
    return mgr;
}

AutoStartManager::AutoStartManager()
{
    // 用户在设置页改 autostart 字段时，立即同步注册表（无论改前是什么状态）。
    // alignFromConfig() 已在 MainWindow 启动时调用过一次，处理"配置 vs 注册表"
    // 的初始偏差；这里是配置驱动的实时同步。
    connect(&ConfigManager::instance(), &ConfigManager::configChanged, this,
            [](const QString &path) {
        if (path != QLatin1String("autostart"))
            return;
        const bool want = ConfigManager::instance().autostart();
        const bool actual = AutoStartManager::instance().isRegistered();
        if (want != actual)
            AutoStartManager::instance().setEnabled(want);
    });
}

bool AutoStartManager::isRegistered() const
{
#ifdef _WIN32
    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_READ, &hKey);
    if (rc != ERROR_SUCCESS)
        return false;
    WCHAR buf[MAX_PATH] = {0};
    DWORD bufSize = sizeof(buf);
    DWORD type = 0;
    rc = RegQueryValueExW(hKey, kRunValueName, nullptr, &type,
                          reinterpret_cast<LPBYTE>(buf), &bufSize);
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ);
#else
    return false;
#endif
}

bool AutoStartManager::setEnabled(bool on)
{
#ifdef _WIN32
    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0,
                            KEY_SET_VALUE, &hKey);
    if (rc != ERROR_SUCCESS)
        return false;

    if (on) {
        const QString cmd = buildRunCommand();
        const std::wstring wcmd = cmd.toStdWString();
        rc = RegSetValueExW(hKey, kRunValueName, 0, REG_SZ,
                            reinterpret_cast<const BYTE *>(wcmd.c_str()),
                            static_cast<DWORD>((wcmd.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(hKey, kRunValueName);
        // 值不存在视为成功（幂等删除）。
        if (rc == ERROR_FILE_NOT_FOUND)
            rc = ERROR_SUCCESS;
    }
    RegCloseKey(hKey);
    return rc == ERROR_SUCCESS;
#else
    Q_UNUSED(on);
    return false;
#endif
}

void AutoStartManager::alignFromConfig()
{
#ifdef _WIN32
    const bool want = ConfigManager::instance().autostart();
    const bool actual = isRegistered();
    if (want == actual)
        return;

    // 配置为准：want=true 但未注册 -> 写入；want=false 但仍注册 -> 删除。
    HKEY hKey = nullptr;
    LONG rc = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0,
                            KEY_SET_VALUE, &hKey);
    if (rc != ERROR_SUCCESS)
        return; // 注册表不可写时静默放弃，不阻塞启动。

    if (want) {
        const QString cmd = buildRunCommand();
        const std::wstring wcmd = cmd.toStdWString();
        RegSetValueExW(hKey, kRunValueName, 0, REG_SZ,
                       reinterpret_cast<const BYTE *>(wcmd.c_str()),
                       static_cast<DWORD>((wcmd.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(hKey, kRunValueName);
        if (rc == ERROR_FILE_NOT_FOUND)
            rc = ERROR_SUCCESS;
    }
    RegCloseKey(hKey);
#endif
}
