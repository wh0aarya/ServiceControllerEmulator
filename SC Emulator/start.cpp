// start.cpp
#include "start.h"
#include "utils.h"

#include <windows.h>
#include <winsvc.h>

#include <iostream>
#include <string>
#include <vector>

static std::string NarrowFromWide(const std::wstring& value)
{
    if (value.empty())
    {
        return std::string();
    }

    int sizeNeeded = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (sizeNeeded <= 0)
    {
        return std::string();
    }

    std::string result(sizeNeeded, '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        &result[0],
        sizeNeeded,
        nullptr,
        nullptr);

    return result;
}

struct StartOptions
{
    bool showHelp = false;
    std::string serviceName;
    std::vector<std::string> serviceArgs;
};

void PrintStartUsage()
{
    std::cout << "DESCRIPTION:\n";
    std::cout << "        Starts a service.\n\n";
    std::cout << "USAGE:\n";
    std::cout << "        sc_template start [service name] <service arguments>\n\n";
    std::cout << "PARAMETERS:\n";
    std::cout << "        service name       Specifies the service name returned by\n";
    std::cout << "                           the getkeyname operation.\n";
    std::cout << "        service arguments  Specifies the service arguments to pass\n";
    std::cout << "                           to the service to be started.\n";
    std::cout << "        /?                 Displays this help message.\n";
}

static bool ParseStartArguments(const std::vector<std::string>& args, StartOptions& opts)
{
    if (args.empty() || !EqualsIgnoreCase(args[0], "start"))
    {
        return false;
    }

    if (args.size() >= 2 && args[1] == "/?")
    {
        opts.showHelp = true;
        return true;
    }

    if (args.size() < 2)
    {
        return false;
    }

    opts.serviceName = args[1];
    if (opts.serviceName.empty())
    {
        return false;
    }

    for (size_t i = 2; i < args.size(); ++i)
    {
        opts.serviceArgs.push_back(args[i]);
    }

    return true;
}

static std::string ServiceStateToString(DWORD state)
{
    switch (state)
    {
    case SERVICE_STOPPED:
        return "1  STOPPED";
    case SERVICE_START_PENDING:
        return "2  START_PENDING";
    case SERVICE_STOP_PENDING:
        return "3  STOP_PENDING";
    case SERVICE_RUNNING:
        return "4  RUNNING";
    case SERVICE_CONTINUE_PENDING:
        return "5  CONTINUE_PENDING";
    case SERVICE_PAUSE_PENDING:
        return "6  PAUSE_PENDING";
    case SERVICE_PAUSED:
        return "7  PAUSED";
    default:
        return std::to_string(state) + "  UNKNOWN";
    }
}

static std::string ControlsAcceptedToString(DWORD controls)
{
    std::vector<std::string> parts;

    if (controls & SERVICE_ACCEPT_STOP)
        parts.push_back("STOPPABLE");
    else
        parts.push_back("NOT_STOPPABLE");

    if (controls & SERVICE_ACCEPT_PAUSE_CONTINUE)
        parts.push_back("PAUSABLE");
    else
        parts.push_back("NOT_PAUSABLE");

    if (controls & SERVICE_ACCEPT_SHUTDOWN)
        parts.push_back("ACCEPTS_SHUTDOWN");
    else
        parts.push_back("IGNORES_SHUTDOWN");

    std::string result;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i != 0)
            result += ", ";
        result += parts[i];
    }

    return result;
}

static bool QueryAndPrintStartStatus(SC_HANDLE service, const std::string& serviceName)
{
    SERVICE_STATUS_PROCESS status = {};
    DWORD bytesNeeded = 0;

    if (!QueryServiceStatusEx(
        service,
        SC_STATUS_PROCESS_INFO,
        reinterpret_cast<LPBYTE>(&status),
        sizeof(status),
        &bytesNeeded))
    {
        std::cout << "SERVICE_NAME: " << serviceName << "\n";
        std::cout << "        STATE              : ?  UNKNOWN\n";
        return false;
    }

    std::cout << "SERVICE_NAME: " << serviceName << "\n";
    std::cout << "        TYPE               : " << status.dwServiceType << "\n";
    std::cout << "        STATE              : " << ServiceStateToString(status.dwCurrentState) << "\n";
    std::cout << "                                ("
        << ControlsAcceptedToString(status.dwControlsAccepted)
        << ")\n";
    std::cout << "        WIN32_EXIT_CODE    : " << status.dwWin32ExitCode << "  (0x"
        << std::hex << status.dwWin32ExitCode << std::dec << ")\n";
    std::cout << "        SERVICE_EXIT_CODE  : " << status.dwServiceSpecificExitCode << "  (0x"
        << std::hex << status.dwServiceSpecificExitCode << std::dec << ")\n";
    std::cout << "        CHECKPOINT         : " << status.dwCheckPoint << "\n";
    std::cout << "        WAIT_HINT          : " << status.dwWaitHint << "\n";

    return true;
}

static bool StartAndPrintService(const StartOptions& opts)
{
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr)
    {
        std::cout << "[SC] OpenSCManager FAILED " << GetLastError() << ":\n";
        return false;
    }

    SC_HANDLE service = OpenServiceA(
        scm,
        opts.serviceName.c_str(),
        SERVICE_START | SERVICE_QUERY_STATUS);

    if (service == nullptr)
    {
        DWORD err = GetLastError();
        std::cout << "[SC] OpenService FAILED " << err << ":\n";
        CloseServiceHandle(scm);
        return false;
    }

    std::vector<LPCSTR> argv;
    argv.reserve(opts.serviceArgs.size());
    for (const auto& arg : opts.serviceArgs)
    {
        argv.push_back(arg.c_str());
    }

    if (!StartServiceA(
        service,
        static_cast<DWORD>(argv.size()),
        argv.empty() ? nullptr : argv.data()))
    {
        DWORD err = GetLastError();
        std::cout << "[SC] StartService FAILED " << err << ":\n";

        LPSTR messageBuffer = nullptr;
        DWORD messageLength = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            err,
            0,
            reinterpret_cast<LPSTR>(&messageBuffer),
            0,
            nullptr);

        if (messageLength != 0 && messageBuffer != nullptr)
        {
            std::cout << messageBuffer;
            LocalFree(messageBuffer);
        }

        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return false;
    }

    QueryAndPrintStartStatus(service, opts.serviceName);

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}

static bool HandleStartCommandImpl(const std::vector<std::string>& args)
{
    StartOptions opts;
    if (!ParseStartArguments(args, opts))
    {
        PrintDefaultError();
        return false;
    }

    if (opts.showHelp)
    {
        PrintStartUsage();
        return true;
    }

    return StartAndPrintService(opts);
}

int HandleStartCommand(const std::vector<std::wstring>& args)
{
    if (args.empty())
    {
        PrintStartUsage();
        return 1;
    }

    if (args.size() == 1 &&
        (args[0] == L"/?" || args[0] == L"-?" || args[0] == L"--help" || args[0] == L"--HELP"))
    {
        PrintStartUsage();
        return 0;
    }

    std::vector<std::string> narrowArgs;
    narrowArgs.reserve(args.size() + 1);
    narrowArgs.push_back("start");

    for (const auto& arg : args)
    {
        narrowArgs.push_back(NarrowFromWide(arg));
    }

    return HandleStartCommandImpl(narrowArgs) ? 0 : 1;
}