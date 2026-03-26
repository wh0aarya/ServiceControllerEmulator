#include "qdescription.h"
#include "utils.h"

#include <windows.h>
#include <winsvc.h>

#include <cctype>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

// Stores parsed arguments for the `qdescription` command.
struct QdescriptionOptions
{
    bool showHelp = false;
    bool invalidBufferArgument = false;
    std::string serviceName;
    DWORD bufferSize = 1024;
};

// Prints usage/help text for the `qdescription` command.
static void PrintQdescriptionUsage()
{
    std::cout << "DESCRIPTION:\n";
    std::cout << "        Displays the description string for a specified service.\n\n";
    std::cout << "USAGE:\n";
    std::cout << "        sc_template qdescription [service name] <buffer size>\n\n";
    std::cout << "PARAMETERS:\n";
    std::cout << "        service name   Specifies the service name returned by the\n";
    std::cout << "                       getkeyname operation.\n";
    std::cout << "        buffer size    Specifies the size (in bytes) of the buffer.\n";
    std::cout << "                       Default is 1024 bytes.\n";
    std::cout << "        /?             Displays this help message.\n";
}

// Strictly parses a decimal string into a DWORD.
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

// Prints a human-readable message for a Win32 error code.
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

// Parses command-line arguments for `qdescription`.
static bool ParseQdescriptionArguments(
    const std::vector<std::string>& args,
    QdescriptionOptions& opts)
{
    if (args.empty() || !EqualsIgnoreCase(args[0], "qdescription"))
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
    if (args.size() >= 3)
    {
        DWORD parsed = 0;
        if (TryParseDwordStrict(args[2], parsed) && parsed > 0)
        {
            opts.bufferSize = parsed;
        }
        // else: ignore like sc.exe
    }


    return true;
}

// Queries and prints the description string for a service.
static bool QueryAndPrintServiceDescription(const QdescriptionOptions& opts)
{
    // Mirror expected error behavior if the buffer-size argument was invalid.
    if (opts.invalidBufferArgument)
    {
        std::cout << "[SC] QueryServiceConfig2 FAILED 1783:\n\n";
        PrintWin32ErrorMessage(1783);
        return false;
    }

    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr)
    {
        DWORD err = GetLastError();
        std::cout << "[SC] OpenSCManager FAILED " << err << ":\n";
        PrintWin32ErrorMessage(err);
        return false;
    }

    SC_HANDLE service = OpenServiceA(scm, opts.serviceName.c_str(), SERVICE_QUERY_CONFIG);
    if (service == nullptr)
    {
        DWORD err = GetLastError();
        std::cout << "[SC] OpenService FAILED " << err << ":\n";
        PrintWin32ErrorMessage(err);
        CloseServiceHandle(scm);
        return false;
    }

    // Allocate a raw byte buffer.
    // If the user gave 0, allocate at least 1 byte to avoid invalid allocation.
    const DWORD bufferSize =
        (opts.bufferSize < sizeof(SERVICE_DESCRIPTIONA))
        ? static_cast<DWORD>(sizeof(SERVICE_DESCRIPTIONA))
        : opts.bufferSize;

    std::unique_ptr<BYTE[]> buffer(new BYTE[bufferSize]);

    DWORD bytesNeeded = 0;
    if (!QueryServiceConfig2A(
        service,
        SERVICE_CONFIG_DESCRIPTION,
        buffer.get(),
        bufferSize,
        &bytesNeeded))
    {
        DWORD err = GetLastError();
        std::cout << "[SC] QueryServiceConfig2 FAILED " << err << ":\n\n";
        PrintWin32ErrorMessage(err);

        if (err == ERROR_INSUFFICIENT_BUFFER)
        {
            std::cout << "\nBytes needed: " << bytesNeeded << "\n";
        }

        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_DESCRIPTIONA* description =
        reinterpret_cast<SERVICE_DESCRIPTIONA*>(buffer.get());

    std::cout << "[SC] QueryServiceConfig2 SUCCESS\n\n";
    std::cout << "SERVICE_NAME: " << opts.serviceName << "\n";

    const char* descText =
        (description->lpDescription != nullptr) ? description->lpDescription : "";

    std::cout << "DESCRIPTION:  " << descText << "\n";

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}

// Entry point for the `qdescription` subcommand.
bool HandleQDescriptionCommand(const std::vector<std::string>& args)
{
    QdescriptionOptions opts;
    if (!ParseQdescriptionArguments(args, opts))
    {
        PrintDefaultError();
        return false;
    }

    if (opts.showHelp)
    {
        PrintQdescriptionUsage();
        return true;
    }

    return QueryAndPrintServiceDescription(opts);
}