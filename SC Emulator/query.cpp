// query.cpp
// Implements the `query` command, which retrieves and prints detailed runtime status for a Windows service.

#include "query.h"
#include "utils.h"

#include <windows.h>
#include <winsvc.h>
#include <iostream>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <cctype>

// Some SDKs already define these. If yours does not, define them here so the code compiles.
#ifndef SERVICE_ADAPTER
#define SERVICE_ADAPTER 0x00000004
#endif

#ifndef SERVICE_RECOGNIZER_DRIVER
#define SERVICE_RECOGNIZER_DRIVER 0x00000008
#endif

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

    // FIRST: check combined WIN32 type
    if ((type & SERVICE_WIN32) == SERVICE_WIN32)
    {
        return "30  WIN32";
    }

    // THEN check specific subtypes (fallback)
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

    return std::to_string(type) + "  UNKNOWN";
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

static std::string ControlsAcceptedToString(DWORD controlsAccepted)
{
    std::vector<std::string> flags;

    if (controlsAccepted & SERVICE_ACCEPT_STOP)
        flags.push_back("STOPPABLE");
    else
        flags.push_back("NOT_STOPPABLE");

    if (controlsAccepted & SERVICE_ACCEPT_PAUSE_CONTINUE)
        flags.push_back("PAUSABLE");
    else
        flags.push_back("NOT_PAUSABLE");

    if (controlsAccepted & SERVICE_ACCEPT_SHUTDOWN)
        flags.push_back("ACCEPTS_SHUTDOWN");
    else
        flags.push_back("IGNORES_SHUTDOWN");

    std::string result;
    for (size_t i = 0; i < flags.size(); ++i)
    {
        result += flags[i];
        if (i + 1 < flags.size())
            result += ", ";
    }

    return result;
}

static void PrintSingleServiceStatus(
    const std::string& serviceName,
    const std::string& displayName,
    DWORD serviceType,
    const SERVICE_STATUS_PROCESS& ssp)
{
    std::cout << "SERVICE_NAME: " << serviceName << "\n";
    std::cout << "DISPLAY_NAME: " << displayName << "\n";
    std::cout << "        TYPE               : " << ServiceTypeToString(serviceType) << "\n";
    std::cout << "        STATE              : " << ServiceStateToString(ssp.dwCurrentState) << "\n";

    if (ssp.dwCurrentState != SERVICE_STOPPED)
    {
        std::cout << "                                (" << ControlsAcceptedToString(ssp.dwControlsAccepted) << ")\n";
    }

    std::cout << "        WIN32_EXIT_CODE    : " << ssp.dwWin32ExitCode << "\n";
    std::cout << "        SERVICE_EXIT_CODE  : " << ssp.dwServiceSpecificExitCode << "\n";
    std::cout << "        CHECKPOINT         : " << ssp.dwCheckPoint << "\n";
    std::cout << "        WAIT_HINT          : " << ssp.dwWaitHint << "\n";
    std::cout << "\n";
}

static bool HandleQuerySingleService(const std::string& serviceName)
{
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr)
    {
        std::cout << "[SC] OpenSCManager FAILED " << GetLastError() << ":\n";
        return false;
    }

    SC_HANDLE svc = OpenServiceA(scm, serviceName.c_str(), SERVICE_QUERY_STATUS);
    if (svc == nullptr)
    {
        std::cout << "[SC] OpenService FAILED " << GetLastError() << ":\n";
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_STATUS_PROCESS ssp{};
    DWORD bytesNeeded = 0;

    if (!QueryServiceStatusEx(
        svc,
        SC_STATUS_PROCESS_INFO,
        reinterpret_cast<LPBYTE>(&ssp),
        sizeof(ssp),
        &bytesNeeded))
    {
        std::cout << "[SC] QueryServiceStatusEx FAILED " << GetLastError() << ":\n";
        CloseServiceHandle(svc);
        CloseServiceHandle(scm);
        return false;
    }

    PrintSingleServiceStatus(serviceName, serviceName, ssp.dwServiceType, ssp);

    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

// Aggregate used to store parsed and normalized command-line options for this command.
struct QueryOptions
{
    bool hasServiceName = false;
    std::string serviceName;

    bool usedEnumOption = false;
    bool userSpecifiedBufsize = false;
    bool userSpecifiedRi = false;

    DWORD enumTypeMask = SERVICE_WIN32;
    bool enumTypeWasExplicit = false;

    DWORD specificTypeMask = 0;

    bool sawInteract = false;
    bool sawOwn = false;
    bool sawShare = false;
    bool sawDriverSubtype = false;

    DWORD serviceState = SERVICE_ACTIVE;
    DWORD bufferSize = 1024;
    DWORD resumeIndex = 0;

    bool hasGroup = false;
    std::string groupName;
};

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

static bool ApplyQueryTypeValue(QueryOptions& opts, const std::string& rawValue)
{
    std::string value = ToLower(rawValue);

    if (value == "driver")
    {
        opts.enumTypeMask = SERVICE_DRIVER;
        opts.enumTypeWasExplicit = true;
        opts.usedEnumOption = true;
        return true;
    }

    if (value == "service")
    {
        opts.enumTypeMask = SERVICE_WIN32;
        opts.enumTypeWasExplicit = true;
        opts.usedEnumOption = true;
        return true;
    }

    if (value == "all")
    {
        opts.enumTypeMask = SERVICE_TYPE_ALL;
        opts.enumTypeWasExplicit = true;
        opts.usedEnumOption = true;
        return true;
    }

    if (value == "own")
    {
        opts.sawOwn = true;
        opts.specificTypeMask |= SERVICE_WIN32_OWN_PROCESS;
        opts.usedEnumOption = true;
        return true;
    }

    if (value == "share" || value == "shared")
    {
        opts.sawShare = true;
        opts.specificTypeMask |= SERVICE_WIN32_SHARE_PROCESS;
        opts.usedEnumOption = true;
        return true;
    }

    if (value == "interact")
    {
        opts.sawInteract = true;
        opts.specificTypeMask |= SERVICE_INTERACTIVE_PROCESS;
        opts.usedEnumOption = true;
        return true;
    }

    if (value == "kernel")
    {
        opts.sawDriverSubtype = true;
        opts.specificTypeMask |= SERVICE_KERNEL_DRIVER;
        opts.usedEnumOption = true;
        return true;
    }

    if (value == "filesys")
    {
        opts.sawDriverSubtype = true;
        opts.specificTypeMask |= SERVICE_FILE_SYSTEM_DRIVER;
        opts.usedEnumOption = true;
        return true;
    }

    if (value == "rec")
    {
        opts.sawDriverSubtype = true;
        opts.specificTypeMask |= SERVICE_RECOGNIZER_DRIVER;
        opts.usedEnumOption = true;
        return true;
    }

    if (value == "adapt")
    {
        opts.sawDriverSubtype = true;
        opts.specificTypeMask |= SERVICE_ADAPTER;
        opts.usedEnumOption = true;
        return true;
    }

    return false;
}

static bool ValidateQueryOptions(QueryOptions& opts)
{
    if (opts.hasServiceName && opts.usedEnumOption)
    {
        return false;
    }

    if (opts.sawInteract && !(opts.sawOwn || opts.sawShare))
    {
        return false;
    }

    if (opts.sawDriverSubtype && !opts.enumTypeWasExplicit)
    {
        opts.enumTypeMask = SERVICE_DRIVER;
    }

    if ((opts.sawOwn || opts.sawShare || opts.sawInteract) && !opts.enumTypeWasExplicit)
    {
        opts.enumTypeMask = SERVICE_WIN32;
    }

    return true;
}

// Parse and validate `query` command arguments.
static bool ParseQueryArguments(const std::vector<std::string>& args, QueryOptions& opts)
{
    if (args.empty() || !EqualsIgnoreCase(args[0], "query"))
    {
        return false;
    }

    size_t i = 1;

    if (i < args.size() && !IsOptionToken(args[i]))
    {
        opts.hasServiceName = true;
        opts.serviceName = args[i];
        ++i;
    }

    while (i < args.size())
    {
        const std::string optionToken = ToLower(args[i]);

        if (!IsOptionToken(optionToken))
        {
            return false;
        }

        const std::string key = optionToken.substr(0, optionToken.size() - 1);

        if (key == "group")
        {
            opts.hasGroup = true;
            opts.usedEnumOption = true;

            if (i + 1 >= args.size() || IsOptionToken(args[i + 1]))
            {
                opts.groupName = "";
                i += 1;
                continue;
            }
            else
            {
                opts.groupName = args[i + 1];
                i += 2;
                continue;
            }
        }

        if (i + 1 >= args.size())
        {
            return false;
        }

        const std::string value = args[i + 1];

        if (key == "type")
        {
            if (!ApplyQueryTypeValue(opts, value))
            {
                return false;
            }
        }
        else if (key == "state")
        {
            const std::string lowered = ToLower(value);
            opts.usedEnumOption = true;

            if (lowered == "inactive")
            {
                opts.serviceState = SERVICE_INACTIVE;
            }
            else if (lowered == "all")
            {
                opts.serviceState = SERVICE_STATE_ALL;
            }
            else
            {
                return false;
            }
        }
        else if (key == "bufsize")
        {
            DWORD parsed = 0;
            if (!TryParseUnsignedDword(value, parsed))
            {
                return false;
            }

            opts.bufferSize = parsed;
            opts.userSpecifiedBufsize = true;
            opts.usedEnumOption = true;
        }
        else if (key == "ri")
        {
            DWORD parsed = 0;
            if (!TryParseUnsignedDword(value, parsed))
            {
                return false;
            }

            opts.resumeIndex = parsed;
            opts.userSpecifiedRi = true;
            opts.usedEnumOption = true;
        }
        else
        {
            return false;
        }

        i += 2;
    }

    return ValidateQueryOptions(opts);
}

static DWORD BuildEnumServiceTypeMask(const QueryOptions& opts)
{
    if (opts.specificTypeMask & SERVICE_ADAPTER)
    {
        return 0;
    }

    DWORD finalMask = opts.enumTypeMask;

    if (opts.specificTypeMask == 0)
    {
        return finalMask;
    }

    if (finalMask == SERVICE_TYPE_ALL)
    {
        return opts.specificTypeMask;
    }

    DWORD combined = finalMask & opts.specificTypeMask;

    if (combined == 0)
    {
        return 0;
    }

    finalMask = combined;

    if (opts.sawInteract)
    {
        finalMask |= SERVICE_INTERACTIVE_PROCESS;
    }

    return finalMask;
}

static void PrintParsedQueryArguments(const QueryOptions& opts)
{
    std::cout << "Parsed command:\n";
    std::cout << "query\n";

    std::cout << "Parsed service name:\n";
    if (opts.hasServiceName)
    {
        std::cout << opts.serviceName << "\n";
    }
    else
    {
        std::cout << "(none)\n";
    }

    std::cout << "Parsed options:\n";

    if (opts.hasServiceName)
    {
        std::cout << "(none)\n";
        return;
    }

    std::cout << "type(enum)= ";
    if (opts.enumTypeMask == SERVICE_WIN32)
        std::cout << "service\n";
    else if (opts.enumTypeMask == SERVICE_DRIVER)
        std::cout << "driver\n";
    else if (opts.enumTypeMask == SERVICE_TYPE_ALL)
        std::cout << "all\n";
    else
        std::cout << opts.enumTypeMask << "\n";

    std::cout << "type(specific)= ";
    if (opts.specificTypeMask == 0)
    {
        std::cout << "(none)\n";
    }
    else
    {
        std::cout << opts.specificTypeMask << "\n";
    }

    std::cout << "state= ";
    if (opts.serviceState == SERVICE_ACTIVE)
        std::cout << "active\n";
    else if (opts.serviceState == SERVICE_INACTIVE)
        std::cout << "inactive\n";
    else if (opts.serviceState == SERVICE_STATE_ALL)
        std::cout << "all\n";
    else
        std::cout << opts.serviceState << "\n";

    std::cout << "bufsize= " << opts.bufferSize << "\n";
    std::cout << "ri= " << opts.resumeIndex << "\n";

    std::cout << "group= ";
    if (opts.hasGroup)
        std::cout << opts.groupName << "\n";
    else
        std::cout << "(none)\n";
}

static bool HandleQueryEnumerate(const QueryOptions& opts)
{
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
    if (scm == nullptr)
    {
        std::cout << "[SC] OpenSCManager FAILED " << GetLastError() << ":\n";
        return false;
    }

    DWORD serviceTypeMask = BuildEnumServiceTypeMask(opts);
    if (serviceTypeMask == 0)
    {
        CloseServiceHandle(scm);
        PrintDefaultError();
        return false;
    }

    const char* groupPtr = nullptr;

    if (opts.hasGroup && !opts.groupName.empty())
    {
        groupPtr = opts.groupName.c_str();
    }

    DWORD resumeHandle = opts.resumeIndex;
    DWORD bufferSize = max(opts.bufferSize, 8192);

    while (true)
    {
        DWORD bytesNeeded = 0;
        DWORD servicesReturned = 0;

        std::unique_ptr<BYTE[]> buffer(new BYTE[bufferSize]);

        BOOL ok = EnumServicesStatusExA(
            scm,
            SC_ENUM_PROCESS_INFO,
            serviceTypeMask,
            opts.serviceState,
            buffer.get(),
            bufferSize,
            &bytesNeeded,
            &servicesReturned,
            &resumeHandle,
            groupPtr);

        ENUM_SERVICE_STATUS_PROCESSA* services =
            reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSA*>(buffer.get());

        for (DWORD i = 0; i < servicesReturned; ++i)
        {
            PrintSingleServiceStatus(
                services[i].lpServiceName ? services[i].lpServiceName : "",
                services[i].lpDisplayName ? services[i].lpDisplayName : "",
                services[i].ServiceStatusProcess.dwServiceType,
                services[i].ServiceStatusProcess);
        }

        if (ok)
        {
            break;
        }

        DWORD err = GetLastError();

        if (err == ERROR_MORE_DATA)
        {
            // Match sc.exe banner style.
            std::cout << "[SC] EnumServicesStatus: more data, need " << bytesNeeded
                << " bytes start resume at index " << resumeHandle << "\n";

            // If the user explicitly asked for a small buffer or resume index,
            // stop here after printing this partial page, like sc.exe does.
            if (opts.userSpecifiedBufsize || opts.userSpecifiedRi)
            {
                CloseServiceHandle(scm);
                return true;
            }

            // Otherwise grow the buffer and continue from the resume handle.
            bufferSize = bytesNeeded;
            continue;
        }

        std::cout << "[SC] EnumServicesStatusEx FAILED " << err << ":\n";
        CloseServiceHandle(scm);
        return false;
    }

    CloseServiceHandle(scm);
    return true;
}

// Open the target service and print its current runtime status.
bool HandleQueryCommand(const std::vector<std::string>& args)
{
    QueryOptions opts;

    if (!ParseQueryArguments(args, opts))
    {
        PrintDefaultError();
        return false;
    }

    PrintParsedQueryArguments(opts);
    std::cout << "-------------------------\n";

    if (opts.hasServiceName)
    {
        return HandleQuerySingleService(opts.serviceName);
    }

    return HandleQueryEnumerate(opts);
}