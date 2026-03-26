#include "delete.h"
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

struct DeleteOptions
{
    bool showHelp = false;
    std::string serviceName;
};

void PrintDeleteUsage()
{
    std::cout << "DESCRIPTION:\n";
    std::cout << "        Deletes a service subkey from the registry.\n";
    std::cout << "        If the service is running or another process has an open\n";
    std::cout << "        handle to it, the service is marked for deletion.\n\n";

    std::cout << "USAGE:\n";
    std::cout << "        sc_template delete [service name]\n\n";

    std::cout << "PARAMETERS:\n";
    std::cout << "        service name   Specifies the service name returned by\n";
    std::cout << "                       the getkeyname operation.\n";
    std::cout << "        /?             Displays this help message.\n";
}

static bool ParseDeleteArguments(const std::vector<std::string>& args, DeleteOptions& opts)
{
    if (args.empty() || !EqualsIgnoreCase(args[0], "delete"))
    {
        return false;
    }

    if (args.size() >= 2 && args[1] == "/?")
    {
        opts.showHelp = true;
        return true;
    }

    // Match sc.exe leniency more closely:
    // require at least one service name token, but ignore extra trailing tokens.
    if (args.size() < 2)
    {
        return false;
    }

    opts.serviceName = args[1];
    return !opts.serviceName.empty();
}

static void PrintWin32ErrorMessage(DWORD err)
{
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
    else
    {
        std::cout << "\n";
    }
}

static bool DeleteServiceByName(const std::string& serviceName)
{
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr)
    {
        DWORD err = GetLastError();
        std::cout << "[SC] OpenSCManager FAILED " << err << ":\n";
        PrintWin32ErrorMessage(err);
        return false;
    }

    SC_HANDLE service = OpenServiceA(scm, serviceName.c_str(), DELETE);
    if (service == nullptr)
    {
        DWORD err = GetLastError();
        std::cout << "[SC] OpenService FAILED " << err << ":\n";
        PrintWin32ErrorMessage(err);
        CloseServiceHandle(scm);
        return false;
    }

    if (!DeleteService(service))
    {
        DWORD err = GetLastError();
        std::cout << "[SC] DeleteService FAILED " << err << ":\n";
        PrintWin32ErrorMessage(err);
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return false;
    }

    std::cout << "[SC] DeleteService SUCCESS\n";

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}

static bool HandleDeleteCommandImpl(const std::vector<std::string>& args)
{
    DeleteOptions opts;

    if (!ParseDeleteArguments(args, opts))
    {
        PrintDefaultError();
        return false;
    }

    if (opts.showHelp)
    {
        PrintDeleteUsage();
        return true;
    }

    std::cout << "Parsed command:\n";
    std::cout << "delete\n";
    std::cout << "Parsed service name:\n";
    std::cout << opts.serviceName << "\n";
    std::cout << "Parsed options:\n";
    std::cout << "(none)\n";
    std::cout << "-------------------------\n";

    return DeleteServiceByName(opts.serviceName);
}

int HandleDeleteCommand(const std::vector<std::wstring>& args)
{
    if (args.empty())
    {
        PrintDeleteUsage();
        return 1;
    }

    if (args.size() == 1 &&
        (args[0] == L"/?" || args[0] == L"-?" || args[0] == L"--help" || args[0] == L"--HELP"))
    {
        PrintDeleteUsage();
        return 0;
    }

    std::vector<std::string> narrowArgs;
    narrowArgs.reserve(args.size() + 1);
    narrowArgs.push_back("delete");

    for (const auto& arg : args)
    {
        narrowArgs.push_back(NarrowFromWide(arg));
    }

    return HandleDeleteCommandImpl(narrowArgs) ? 0 : 1;
}