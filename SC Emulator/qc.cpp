#include "qc.h"
#include "utils.h"

#include <windows.h>
#include <winsvc.h>

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <cctype>

// Prints usage/help text for the `qc` command.
static void PrintQcUsage()
{
    std::cout << "DESCRIPTION:\n";
    std::cout << "        Queries the configuration information for a service.\n\n";
    std::cout << "USAGE:\n";
    std::cout << "        sc_template qc [service name] <buffer size>\n\n";
    std::cout << "PARAMETERS:\n";
    std::cout << "        service name   Specifies the service name.\n";
    std::cout << "        buffer size    Specifies the buffer size in bytes.\n";
    std::cout << "                       Default is 1024 bytes.\n";
    std::cout << "        /?             Displays this help message.\n";
}

// Stores parsed arguments for the `qc` command.
struct QcOptions
{
    bool showHelp = false;
    std::string serviceName;
    DWORD bufferSize = 1024;
};

// Parses a positive decimal number into a DWORD.
static bool TryParseUnsignedDword(const std::string& text, DWORD& valueOut)
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

// Parses all arguments for the `qc` command.
// A third token may optionally override the default buffer size.
static bool ParseQcArguments(const std::vector<std::string>& args, QcOptions& opts)
{
    if (args.empty() || !EqualsIgnoreCase(args[0], "qc"))
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

    // sc.exe behavior:
    // - if a third token exists and is a positive integer, use it as buffer size
    // - otherwise ignore it and keep default 1024
    if (args.size() >= 3)
    {
        DWORD parsed = 0;
        if (TryParseUnsignedDword(args[2], parsed) && parsed > 0)
        {
            opts.bufferSize = parsed;
        }
    }

    // Ignore any extra arguments after the buffer-size position.
    return true;
}

// Converts the raw service type into a readable string like sc.exe output.
static std::string ServiceTypeToString(DWORD type)
{
    if (type == SERVICE_KERNEL_DRIVER)
    {
        return "1  KERNEL_DRIVER";
    }

    if (type == SERVICE_FILE_SYSTEM_DRIVER)
    {
        return "2  FILE_SYSTEM_DRIVER";
    }

    if (type == SERVICE_RECOGNIZER_DRIVER)
    {
        return "8  RECOGNIZER_DRIVER";
    }

    if (type == SERVICE_ADAPTER)
    {
        return "4  ADAPTER";
    }

    if ((type & SERVICE_WIN32_OWN_PROCESS) == SERVICE_WIN32_OWN_PROCESS)
    {
        if (type & SERVICE_INTERACTIVE_PROCESS)
        {
            return std::to_string(type) + "  WIN32_OWN_PROCESS (INTERACTIVE)";
        }

        return "10  WIN32_OWN_PROCESS";
    }

    if ((type & SERVICE_WIN32_SHARE_PROCESS) == SERVICE_WIN32_SHARE_PROCESS)
    {
        if (type & SERVICE_INTERACTIVE_PROCESS)
        {
            return std::to_string(type) + "  WIN32_SHARE_PROCESS (INTERACTIVE)";
        }

        return "20  WIN32_SHARE_PROCESS";
    }

    if (type == SERVICE_WIN32)
    {
        return "30  WIN32";
    }

    return std::to_string(type) + "  UNKNOWN";
}

// Converts the service start type into a readable label.
static std::string StartTypeToString(DWORD startType)
{
    switch (startType)
    {
    case SERVICE_BOOT_START:
        return "0   BOOT_START";
    case SERVICE_SYSTEM_START:
        return "1   SYSTEM_START";
    case SERVICE_AUTO_START:
        return "2   AUTO_START";
    case SERVICE_DEMAND_START:
        return "3   DEMAND_START";
    case SERVICE_DISABLED:
        return "4   DISABLED";
    default:
        return std::to_string(startType) + "   UNKNOWN";
    }
}

// Converts service error-control policy into a readable label.
static std::string ErrorControlToString(DWORD errorControl)
{
    switch (errorControl)
    {
    case SERVICE_ERROR_IGNORE:
        return "0   IGNORE";
    case SERVICE_ERROR_NORMAL:
        return "1   NORMAL";
    case SERVICE_ERROR_SEVERE:
        return "2   SEVERE";
    case SERVICE_ERROR_CRITICAL:
        return "3   CRITICAL";
    default:
        return std::to_string(errorControl) + "   UNKNOWN";
    }
}

// Prints dependency strings from a MULTI_SZ list.
static void PrintDependencies(LPCSTR multiSz)
{
    if (multiSz == nullptr || *multiSz == '\0')
    {
        std::cout << "        DEPENDENCIES       : (none)\n";
        return;
    }

    const char* current = multiSz;
    bool first = true;

    while (*current != '\0')
    {
        if (first)
        {
            std::cout << "        DEPENDENCIES       : " << current << "\n";
            first = false;
        }
        else
        {
            std::cout << "                           : " << current << "\n";
        }

        current += std::strlen(current) + 1;
    }
}

// Queries the SCM for a service's configuration and prints it.
static bool QueryAndPrintServiceConfig(const QcOptions& opts)
{
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr)
    {
        std::cout << "[SC] OpenSCManager FAILED " << GetLastError() << ":\n";
        return false;
    }

    SC_HANDLE service = OpenServiceA(scm, opts.serviceName.c_str(), SERVICE_QUERY_CONFIG);
    if (service == nullptr)
    {
        std::cout << "[SC] OpenService FAILED " << GetLastError() << ":\n";
        CloseServiceHandle(scm);
        return false;
    }

    std::unique_ptr<BYTE[]> buffer(new BYTE[opts.bufferSize]);
    QUERY_SERVICE_CONFIGA* config =
        reinterpret_cast<QUERY_SERVICE_CONFIGA*>(buffer.get());

    DWORD bytesNeeded = 0;

    if (!QueryServiceConfigA(service, config, opts.bufferSize, &bytesNeeded))
    {
        DWORD err = GetLastError();
        std::cout << "[SC] QueryServiceConfig FAILED " << err << ":\n\n";

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
            std::cout << messageBuffer << "\n";
            LocalFree(messageBuffer);
        }

        if (err == ERROR_INSUFFICIENT_BUFFER)
        {
            std::cout << "[SC] GetServiceConfig needs " << bytesNeeded << " bytes\n";
        }

        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return false;
    }

    std::cout << "[SC] QueryServiceConfig SUCCESS\n\n";
    std::cout << "SERVICE_NAME: " << opts.serviceName << "\n";
    std::cout << "        TYPE               : " << ServiceTypeToString(config->dwServiceType) << "\n";
    std::cout << "        START_TYPE         : " << StartTypeToString(config->dwStartType) << "\n";
    std::cout << "        ERROR_CONTROL      : " << ErrorControlToString(config->dwErrorControl) << "\n";
    std::cout << "        BINARY_PATH_NAME   : "
        << (config->lpBinaryPathName ? config->lpBinaryPathName : "") << "\n";
    std::cout << "        LOAD_ORDER_GROUP   : "
        << ((config->lpLoadOrderGroup && *config->lpLoadOrderGroup) ? config->lpLoadOrderGroup : "") << "\n";
    std::cout << "        TAG                : " << config->dwTagId << "\n";
    std::cout << "        DISPLAY_NAME       : "
        << (config->lpDisplayName ? config->lpDisplayName : "") << "\n";
    PrintDependencies(config->lpDependencies);
    std::cout << "        SERVICE_START_NAME : "
        << (config->lpServiceStartName ? config->lpServiceStartName : "") << "\n";

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}

// Entry point for the `qc` subcommand.
bool HandleQcCommand(const std::vector<std::string>& args)
{
    QcOptions opts;

    if (!ParseQcArguments(args, opts))
    {
        PrintDefaultError();
        return false;
    }

    if (opts.showHelp)
    {
        PrintQcUsage();
        return true;
    }

    // Optional debug-style output showing parsed arguments.
    std::cout << "Parsed command:\n";
    std::cout << "qc\n";
    std::cout << "Parsed service name:\n";
    std::cout << opts.serviceName << "\n";
    std::cout << "Parsed options:\n";
    std::cout << "buffersize= " << opts.bufferSize << "\n";
    std::cout << "-------------------------\n";

    return QueryAndPrintServiceConfig(opts);
}