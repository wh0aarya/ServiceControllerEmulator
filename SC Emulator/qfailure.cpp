// qfailure.cpp

#include "qfailure.h"
#include "utils.h"

#include <windows.h>
#include <winsvc.h>

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cctype>

struct QfailureOptions
{
    bool showHelp = false;
    std::string serviceName;
    DWORD bufferSize = 1024;
};

static void PrintQfailureUsage()
{
    std::cout << "DESCRIPTION:\n";
    std::cout << "        Queries the failure actions for a service.\n\n";
    std::cout << "USAGE:\n";
    std::cout << "        sc_template qfailure [service name] <buffer size>\n\n";
    std::cout << "PARAMETERS:\n";
    std::cout << "        service name   Specifies the service name returned by the\n";
    std::cout << "                       getkeyname operation.\n";
    std::cout << "        buffer size    Specifies the size (in bytes) of the buffer.\n";
    std::cout << "                       Default is 1024 bytes.\n";
    std::cout << "        /?             Displays this help message.\n";
}

static bool TryParseDwordStrict(const std::string& text, DWORD& valueOut)
{
    if (text.empty())
    {
        return false;
    }

    unsigned long long value = 0;
    for (char c : text)
    {
        if (!std::isdigit(static_cast<unsigned char>(c)))
        {
            return false;
        }

        value = (value * 10ULL) + static_cast<unsigned long long>(c - '0');
        if (value > 0xFFFFFFFFULL)
        {
            return false;
        }
    }

    valueOut = static_cast<DWORD>(value);
    return true;
}

static void PrintWin32ErrorMessage(DWORD errorCode)
{
    LPSTR messageBuffer = nullptr;
    DWORD messageLength = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        0,
        reinterpret_cast<LPSTR>(&messageBuffer),
        0,
        nullptr);

    if (messageLength != 0 && messageBuffer != nullptr)
    {
        std::cout << messageBuffer;
        LocalFree(messageBuffer);
    }
}

static bool ParseQfailureArguments(
    const std::vector<std::string>& args,
    QfailureOptions& opts)
{
    if (args.empty() || !EqualsIgnoreCase(args[0], "qfailure"))
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

    // ✅ FIX: ignore invalid buffer argument
    if (args.size() >= 3)
    {
        DWORD parsed = 0;
        if (TryParseDwordStrict(args[2], parsed) && parsed > 0)
        {
            opts.bufferSize = parsed;
        }
        // else ignore like sc.exe
    }

    return true;
}

static std::string FailureActionTypeToString(SC_ACTION_TYPE type)
{
    switch (type)
    {
    case SC_ACTION_NONE: return "NONE";
    case SC_ACTION_RESTART: return "RESTART";
    case SC_ACTION_REBOOT: return "REBOOT";
    case SC_ACTION_RUN_COMMAND: return "RUN_COMMAND";
    default: return "UNKNOWN";
    }
}

static void PrintFailureActions(const SERVICE_FAILURE_ACTIONSA* actions)
{
    std::cout << "        FAILURE_ACTIONS              : ";

    if (!actions || actions->cActions == 0 || !actions->lpsaActions)
    {
        std::cout << "(none)\n";
        return;
    }

    for (DWORD i = 0; i < actions->cActions; ++i)
    {
        const SC_ACTION& action = actions->lpsaActions[i];

        if (i == 0)
        {
            std::cout << FailureActionTypeToString(action.Type)
                << " -- Delay = " << action.Delay << " milliseconds.\n";
        }
        else
        {
            std::cout << "                                       "
                << FailureActionTypeToString(action.Type)
                << " -- Delay = " << action.Delay << " milliseconds.\n";
        }
    }
}

static bool QueryAndPrintFailureActions(const QfailureOptions& opts)
{
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm)
    {
        DWORD err = GetLastError();
        std::cout << "[SC] OpenSCManager FAILED " << err << ":\n";
        PrintWin32ErrorMessage(err);
        return false;
    }

    SC_HANDLE service = OpenServiceA(scm, opts.serviceName.c_str(), SERVICE_QUERY_CONFIG);
    if (!service)
    {
        DWORD err = GetLastError();
        std::cout << "[SC] OpenService FAILED " << err << ":\n";
        PrintWin32ErrorMessage(err);
        CloseServiceHandle(scm);
        return false;
    }

    DWORD bufferSize = max(opts.bufferSize, sizeof(SERVICE_FAILURE_ACTIONSA));
    std::unique_ptr<BYTE[]> buffer(new BYTE[bufferSize]);

    DWORD bytesNeeded = 0;
    if (!QueryServiceConfig2A(
        service,
        SERVICE_CONFIG_FAILURE_ACTIONS,
        buffer.get(),
        bufferSize,
        &bytesNeeded))
    {
        DWORD err = GetLastError();
        std::cout << "[SC] QueryServiceConfig2 FAILED " << err << ":\n\n";
        PrintWin32ErrorMessage(err);

        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return false;
    }

    auto* failureActions = reinterpret_cast<SERVICE_FAILURE_ACTIONSA*>(buffer.get());

    std::cout << "[SC] QueryServiceConfig2 SUCCESS\n\n";
    std::cout << "SERVICE_NAME: " << opts.serviceName << "\n";
    std::cout << "        RESET_PERIOD (in seconds)    : " << failureActions->dwResetPeriod << "\n";

    std::cout << "        REBOOT_MESSAGE               : "
        << (failureActions->lpRebootMsg ? failureActions->lpRebootMsg : "") << "\n";

    std::cout << "        COMMAND_LINE                 : "
        << (failureActions->lpCommand ? failureActions->lpCommand : "") << "\n";

    PrintFailureActions(failureActions);

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}

bool HandleQFailureCommand(const std::vector<std::string>& args)
{
    QfailureOptions opts;
    if (!ParseQfailureArguments(args, opts))
    {
        PrintDefaultError();
        return false;
    }

    if (opts.showHelp)
    {
        PrintQfailureUsage();
        return true;
    }

    return QueryAndPrintFailureActions(opts);
}