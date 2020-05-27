#ifdef _WIN32

#include <string>
#include <stdexcept>
#include <array>
#include <windows.h>

using namespace std;

#define SERVICE_NAME L"TDSweb"
SERVICE_STATUS_HANDLE service_handle = nullptr;

static __inline u16string utf8_to_utf16(const string_view& s) {
    u16string ret;

    if (s.empty())
        return u"";

    auto len = MultiByteToWideChar(CP_UTF8, 0, s.data(), s.length(), nullptr, 0);

    if (len == 0)
        throw runtime_error("MultiByteToWideChar 1 failed.");

    ret.resize(len);

    len = MultiByteToWideChar(CP_UTF8, 0, s.data(), s.length(), (wchar_t*)ret.data(), len);

    if (len == 0)
        throw runtime_error("MultiByteToWideChar 2 failed.");

    return ret;
}

static __inline string utf16_to_utf8(const u16string_view& s) {
    string ret;

    if (s.empty())
        return "";

    auto len = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)s.data(), s.length(), nullptr, 0,
                                   nullptr, nullptr);

    if (len == 0)
        throw runtime_error("WideCharToMultiByte 1 failed.");

    ret.resize(len);

    len = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)s.data(), s.length(), ret.data(), len,
                              nullptr, nullptr);

    if (len == 0)
        throw runtime_error("WideCharToMultiByte 2 failed.");

    return ret;
}

void event_log(const string_view& msg, unsigned short type) {
    HANDLE event_source = RegisterEventSourceW(nullptr, SERVICE_NAME);

    if (event_source) {
        auto wmsg = utf8_to_utf16(msg);
        array<LPCWSTR, 2> strings;

        strings[0] = SERVICE_NAME;
        strings[1] = (LPCWSTR)wmsg.c_str();

        ReportEventW(event_source, type, 0, 0, nullptr, (WORD)strings.size(), 0, &strings[0], nullptr);

        DeregisterEventSource(event_source);
    }
}

class last_error : public exception {
public:
    last_error(const string_view& function, int le) {
        string nice_msg;

        {
            char16_t* fm;

            if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
                le, 0, reinterpret_cast<LPWSTR>(&fm), 0, nullptr)) {
                try {
                    u16string_view s = fm;

                    while (!s.empty() && (s[s.length() - 1] == u'\r' || s[s.length() - 1] == u'\n')) {
                        s.remove_suffix(1);
                    }

                    nice_msg = utf16_to_utf8(s);
                } catch (...) {
                    LocalFree(fm);
                    throw;
                }

                LocalFree(fm);
                }
        }

        msg = string(function) + " failed (error " + to_string(le) + (!nice_msg.empty() ? (", " + nice_msg) : "") + ").";
    }

    const char* what() const noexcept {
        return msg.c_str();
    }

private:
    string msg;
};

void service_install() {
    SC_HANDLE sc_manager;
    wstring pathw;

    {
        WCHAR path[MAX_PATH];

        if (GetModuleFileNameW(nullptr, path, sizeof(path) / sizeof(WCHAR)) == 0)
            throw last_error("GetModuleFileName", GetLastError());

        pathw = L"\""s + path + L"\" service"s;
    }

    sc_manager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (!sc_manager)
        throw last_error("OpenSCManager", GetLastError());

    try {
        auto service = CreateServiceW(sc_manager, SERVICE_NAME, L"TDSweb", SERVICE_QUERY_STATUS,
                                      SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL,
                                      pathw.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);

        if (!sc_manager) {
            CloseServiceHandle(service);
            throw last_error("CreateService", GetLastError());
        }

        CloseServiceHandle(service);
    } catch (...) {
        CloseServiceHandle(sc_manager);
        throw;
    }

    CloseServiceHandle(sc_manager);
}

void service_uninstall() {
    SC_HANDLE sc_manager;

    sc_manager = OpenSCManager(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!sc_manager)
        throw last_error("OpenSCManager", GetLastError());

    try {
        auto service = OpenServiceW(sc_manager, SERVICE_NAME, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE);
        if (!service)
            throw last_error("OpenService", GetLastError());

        try {
            SERVICE_STATUS status = {};

            if (ControlService(service, SERVICE_CONTROL_STOP, &status)) {
                Sleep(1000);

                while (QueryServiceStatus(service, &status)) {
                    if (status.dwCurrentState == SERVICE_STOP_PENDING)
                        Sleep(1000);
                    else
                        break;
                }

                if (status.dwCurrentState != SERVICE_STOPPED)
                    throw runtime_error("Service failed to stop.");
            }

            if (!DeleteService(service))
                throw last_error("DeleteService", GetLastError());
        } catch (...) {
            CloseServiceHandle(service);
            throw;
        }

        CloseServiceHandle(service);
    } catch (...) {
        CloseServiceHandle(sc_manager);
        throw;
    }

    CloseServiceHandle(sc_manager);
}

static void set_status(unsigned long state) {
    SERVICE_STATUS status;

    memset(&status, 0, sizeof(status));
    status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    status.dwCurrentState = state;
    status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN;

    if (!SetServiceStatus(service_handle, &status))
        throw last_error("SetServiceStatus", GetLastError());
}

static unsigned long handler_func(unsigned long control, unsigned long event_type, void* event_data, void* context) {
    switch (control) {
        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_SHUTDOWN:
        {
            // FIXME - stop

            set_status(SERVICE_STOPPED);

            return NO_ERROR;
        }

        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;
    }

    return ERROR_CALL_NOT_IMPLEMENTED;
}

void service() {
    try {
        service_handle = RegisterServiceCtrlHandlerExW(SERVICE_NAME, handler_func, nullptr);

        if (!service_handle)
            throw last_error("RegisterServiceCtrlHandler", GetLastError());

        set_status(SERVICE_START_PENDING);

        throw runtime_error("FIXME - get server and port from Registry");
        // FIXME - get server and port
        // FIXME - call init
        // FIXME - call set_status(SERVICE_STARTED);
    } catch (const exception& e) {
        event_log(e.what(), EVENTLOG_ERROR_TYPE);
        throw;
    }
}

#endif
