// stop.cpp
// Implements the `stop` command, which sends a stop control and waits for the service to stop.

#include "stop.h"

#include <windows.h>
#include <winsvc.h>

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <algorithm>
#include <cwctype>

namespace
{
    class ScHandle
    {
    public:
        ScHandle() noexcept : handle_(nullptr) {}
        explicit ScHandle(SC_HANDLE handle) noexcept : handle_(handle) {}

        ~ScHandle()
        {
            if (handle_ != nullptr)
            {
                CloseServiceHandle(handle_);
            }
        }

        ScHandle(const ScHandle&) = delete;
        ScHandle& operator=(const ScHandle&) = delete;

        ScHandle(ScHandle&& other) noexcept : handle_(other.handle_)
        {
            other.handle_ = nullptr;
        }

        ScHandle& operator=(ScHandle&& other) noexcept
        {
            if (this != &other)
            {
                if (handle_ != nullptr)
                {
                    CloseServiceHandle(handle_);
                }
                handle_ = other.handle_;
                other.handle_ = nullptr;
            }
            return *this;
        }

        SC_HANDLE get() const noexcept { return handle_; }
        explicit operator bool() const noexcept { return handle_ != nullptr; }

    private:
        SC_HANDLE handle_;
    };

    // Return an uppercase copy of a wide string so command checks can be case-insensitive.
    std::wstring ToUpperCopy(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](wchar_t ch) { return static_cast<wchar_t>(towupper(ch)); });
        return value;
    }

    // Recognize the common help switches accepted by the command.
    bool IsHelpArg(const std::wstring& arg)
    {
        const std::wstring upper = ToUpperCopy(arg);
        return upper == L"/?" || upper == L"-?" || upper == L"--HELP";
    }

    // Convert a Win32 error code into a trimmed wide-string message.
    std::wstring Win32ErrorToString(DWORD error)
    {
        LPWSTR buffer = nullptr;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS;

        const DWORD length = FormatMessageW(
            flags,
            nullptr,
            error,
            0,
            reinterpret_cast<LPWSTR>(&buffer),
            0,
            nullptr);

        std::wstring result;
        if (length != 0 && buffer != nullptr)
        {
            result.assign(buffer, length);

            while (!result.empty() &&
                (result.back() == L'\r' || result.back() == L'\n' || result.back() == L' '))
            {
                result.pop_back();
            }
        }
        else
        {
            result = L"Unknown error";
        }

        if (buffer != nullptr)
        {
            LocalFree(buffer);
        }

        return result;
    }

    // Map a service state constant to a readable label.
    const wchar_t* ServiceStateToText(DWORD state)
    {
        switch (state)
        {
        case SERVICE_STOPPED:          return L"STOPPED";
        case SERVICE_START_PENDING:    return L"START_PENDING";
        case SERVICE_STOP_PENDING:     return L"STOP_PENDING";
        case SERVICE_RUNNING:          return L"RUNNING";
        case SERVICE_CONTINUE_PENDING: return L"CONTINUE_PENDING";
        case SERVICE_PAUSE_PENDING:    return L"PAUSE_PENDING";
        case SERVICE_PAUSED:           return L"PAUSED";
        default:                       return L"UNKNOWN";
        }
    }

    // Map a service type constant to a readable label.
    std::wstring ServiceTypeToText(DWORD type)
    {
        switch (type)
        {
        case SERVICE_KERNEL_DRIVER:         return L"KERNEL_DRIVER";
        case SERVICE_FILE_SYSTEM_DRIVER:    return L"FILE_SYSTEM_DRIVER";
        case SERVICE_WIN32_OWN_PROCESS:     return L"WIN32_OWN_PROCESS";
        case SERVICE_WIN32_SHARE_PROCESS:   return L"WIN32_SHARE_PROCESS";
        case SERVICE_INTERACTIVE_PROCESS:   return L"INTERACTIVE_PROCESS";
        default:
            if ((type & SERVICE_WIN32_OWN_PROCESS) && (type & SERVICE_INTERACTIVE_PROCESS))
                return L"WIN32_OWN_PROCESS (interactive)";
            if ((type & SERVICE_WIN32_SHARE_PROCESS) && (type & SERVICE_INTERACTIVE_PROCESS))
                return L"WIN32_SHARE_PROCESS (interactive)";
            return L"UNKNOWN";
        }
    }

    // Describe which control requests the service currently accepts.
    std::wstring ControlsAcceptedToText(DWORD controlsAccepted, DWORD currentState)
    {
        if (currentState == SERVICE_STOP_PENDING)
        {
            return L"";
        }

        std::wstring text;
        auto appendFlag = [&](const wchar_t* flag)
            {
                if (!text.empty())
                {
                    text += L", ";
                }
                text += flag;
            };

        if ((controlsAccepted & SERVICE_ACCEPT_STOP) == 0)
            appendFlag(L"NOT_STOPPABLE");
        if ((controlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE) == 0)
            appendFlag(L"NOT_PAUSABLE");
        if ((controlsAccepted & SERVICE_ACCEPT_SHUTDOWN) != 0)
            appendFlag(L"ACCEPTS_SHUTDOWN");
        else
            appendFlag(L"IGNORES_SHUTDOWN");

        return text;
    }

    // Wrapper around `QueryServiceStatusEx` that fills a `SERVICE_STATUS_PROCESS` structure.
    bool QueryStatus(SC_HANDLE service, SERVICE_STATUS_PROCESS& ssp)
    {
        DWORD bytesNeeded = 0;
        ZeroMemory(&ssp, sizeof(ssp));

        return QueryServiceStatusEx(
            service,
            SC_STATUS_PROCESS_INFO,
            reinterpret_cast<LPBYTE>(&ssp),
            sizeof(ssp),
            &bytesNeeded) != FALSE;
    }

    // Print a formatted service status block similar to `sc query`.
    void PrintServiceStatus(const std::wstring& serviceName, const SERVICE_STATUS_PROCESS& ssp)
    {
        std::wcout << L"SERVICE_NAME: " << serviceName << L"\n";
        std::wcout << L"        TYPE               : " << ssp.dwServiceType
            << L"  " << ServiceTypeToText(ssp.dwServiceType) << L"\n";
        std::wcout << L"        STATE              : " << ssp.dwCurrentState
            << L"  " << ServiceStateToText(ssp.dwCurrentState) << L"\n";

        const std::wstring controls = ControlsAcceptedToText(
            ssp.dwControlsAccepted,
            ssp.dwCurrentState);

        if (!controls.empty())
        {
            std::wcout << L"                                (" << controls << L")\n";
        }

        std::wcout << L"        WIN32_EXIT_CODE    : " << ssp.dwWin32ExitCode
            << L"  (0x" << std::hex << std::uppercase << ssp.dwWin32ExitCode
            << std::dec << std::nouppercase << L")\n";

        std::wcout << L"        SERVICE_EXIT_CODE  : " << ssp.dwServiceSpecificExitCode
            << L"  (0x" << std::hex << std::uppercase << ssp.dwServiceSpecificExitCode
            << std::dec << std::nouppercase << L")\n";

        std::wcout << L"        CHECKPOINT         : " << ssp.dwCheckPoint << L"\n";
        std::wcout << L"        WAIT_HINT          : " << ssp.dwWaitHint << L"\n";
    }

    // Choose a polling interval based on the service wait hint while keeping it within a sensible range.
    DWORD ComputeWaitMillis(DWORD waitHint)
    {
        DWORD waitTime = waitHint / 10;

        if (waitTime < 1000)
            waitTime = 1000;
        else if (waitTime > 10000)
            waitTime = 10000;

        return waitTime;
    }

    int PrintOpenScmError()
    {
        const DWORD err = GetLastError();
        std::wcerr << L"[SC] OpenSCManager FAILED " << err
            << L": " << Win32ErrorToString(err) << L"\n";
        return static_cast<int>(err == 0 ? 1 : err);
    }

    int PrintOpenServiceError(const std::wstring& serviceName)
    {
        const DWORD err = GetLastError();
        std::wcerr << L"[SC] OpenService FAILED " << err
            << L": " << Win32ErrorToString(err) << L"\n";

        if (err == ERROR_SERVICE_DOES_NOT_EXIST)
        {
            std::wcerr << L"Service not found: " << serviceName << L"\n";
        }

        return static_cast<int>(err == 0 ? 1 : err);
    }

    int PrintControlServiceError()
    {
        const DWORD err = GetLastError();
        std::wcerr << L"[SC] ControlService FAILED " << err
            << L": " << Win32ErrorToString(err) << L"\n";

        if (err == ERROR_SERVICE_CANNOT_ACCEPT_CTRL)
        {
            std::wcerr << L"The service cannot accept stop control at this time.\n";
        }
        else if (err == ERROR_DEPENDENT_SERVICES_RUNNING)
        {
            std::wcerr << L"Dependent services are running.\n";
        }
        else if (err == ERROR_INVALID_SERVICE_CONTROL)
        {
            std::wcerr << L"Not all services can be stopped.\n";
        }

        return static_cast<int>(err == 0 ? 1 : err);
    }

    int StopServiceByName(const std::wstring& serviceName)
    {
        ScHandle scm(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
        if (!scm)
        {
            return PrintOpenScmError();
        }

        const DWORD desiredAccess =
            SERVICE_STOP |
            SERVICE_QUERY_STATUS |
            SERVICE_ENUMERATE_DEPENDENTS;

        ScHandle service(OpenServiceW(scm.get(), serviceName.c_str(), desiredAccess));
        if (!service)
        {
            return PrintOpenServiceError(serviceName);
        }

        SERVICE_STATUS_PROCESS ssp{};
        if (!QueryStatus(service.get(), ssp))
        {
            const DWORD err = GetLastError();
            std::wcerr << L"[SC] QueryServiceStatusEx FAILED " << err
                << L": " << Win32ErrorToString(err) << L"\n";
            return static_cast<int>(err == 0 ? 1 : err);
        }

        if (ssp.dwCurrentState == SERVICE_STOPPED)
        {
            PrintServiceStatus(serviceName, ssp);
            return 0;
        }

        if ((ssp.dwControlsAccepted & SERVICE_ACCEPT_STOP) == 0)
        {
            PrintServiceStatus(serviceName, ssp);
            std::wcerr << L"[SC] ControlService FAILED " << ERROR_INVALID_SERVICE_CONTROL
                << L": " << Win32ErrorToString(ERROR_INVALID_SERVICE_CONTROL) << L"\n";
            std::wcerr << L"Not all services can be stopped.\n";
            return ERROR_INVALID_SERVICE_CONTROL;
        }

        SERVICE_STATUS status{};
        if (!ControlService(service.get(), SERVICE_CONTROL_STOP, &status))
        {
            return PrintControlServiceError();
        }

        // Show immediate post-stop-request status.
        if (!QueryStatus(service.get(), ssp))
        {
            const DWORD err = GetLastError();
            std::wcerr << L"[SC] QueryServiceStatusEx FAILED " << err
                << L": " << Win32ErrorToString(err) << L"\n";
            return static_cast<int>(err == 0 ? 1 : err);
        }

        PrintServiceStatus(serviceName, ssp);

        // Wait until fully stopped or timeout.
        const DWORD startTick = GetTickCount();
        DWORD oldCheckpoint = ssp.dwCheckPoint;

        while (ssp.dwCurrentState == SERVICE_STOP_PENDING)
        {
            const DWORD waitMillis = ComputeWaitMillis(ssp.dwWaitHint);
            Sleep(waitMillis);

            if (!QueryStatus(service.get(), ssp))
            {
                const DWORD err = GetLastError();
                std::wcerr << L"[SC] QueryServiceStatusEx FAILED " << err
                    << L": " << Win32ErrorToString(err) << L"\n";
                return static_cast<int>(err == 0 ? 1 : err);
            }

            if (ssp.dwCurrentState == SERVICE_STOPPED)
            {
                return 0;
            }

            if (ssp.dwCheckPoint > oldCheckpoint)
            {
                oldCheckpoint = ssp.dwCheckPoint;
            }
            else
            {
                if (GetTickCount() - startTick > ssp.dwWaitHint + 10000)
                {
                    std::wcerr << L"[SC] StopService TIMED OUT\n";
                    PrintServiceStatus(serviceName, ssp);
                    return ERROR_TIMEOUT;
                }
            }
        }

        if (ssp.dwCurrentState == SERVICE_STOPPED)
        {
            return 0;
        }

        PrintServiceStatus(serviceName, ssp);
        std::wcerr << L"[SC] Service did not reach the STOPPED state.\n";
        return 1;
    }
}

// Print help text for the `stop` command.
void PrintStopUsage()
{
    std::wcout
        << L"DESCRIPTION:\n"
        << L"        Sends a STOP control request to a service.\n"
        << L"        Not all services can be stopped.\n\n"
        << L"USAGE:\n"
        << L"        sc stop [service name]\n\n"
        << L"PARAMETERS:\n"
        << L"        <ServiceName>\n"
        << L"            Specifies the service name returned by the getkeyname operation.\n\n"
        << L"        /?\n"
        << L"            Displays help at the command prompt.\n";
}

// Open the target service, request a stop, and wait until the service reaches the stopped state.
int HandleStopCommand(const std::vector<std::wstring>& args)
{
    if (args.empty())
    {
        PrintStopUsage();
        return 1;
    }

    if (args.size() == 1 && IsHelpArg(args[0]))
    {
        PrintStopUsage();
        return 0;
    }

    // Match sc.exe leniency:
    // require at least one service name token, ignore trailing extras.
    if (args.size() < 1)
    {
        std::wcerr << L"[SC] Invalid command line syntax.\n\n";
        PrintStopUsage();
        return 1;
    }

    const std::wstring& serviceName = args[0];
    return StopServiceByName(serviceName);
}