// create.cpp
// Implements the `create` command, which validates CLI options and creates a service in the SCM database.

#include "create.h"
#include "utils.h"

#include <windows.h>
#include <winsvc.h>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <cctype>

#ifndef SERVICE_INTERACTIVE_PROCESS
#define SERVICE_INTERACTIVE_PROCESS 0x00000100
#endif

// Aggregate used to store parsed and normalized command-line options for this command.
struct CreateOptions
{
    bool showHelp = false;
    std::string serviceName;

    DWORD serviceType = SERVICE_WIN32_OWN_PROCESS;
    bool sawOwn = false;
    bool sawShare = false;
    bool sawKernel = false;
    bool sawFilesys = false;
    bool sawRec = false;
    bool sawInteract = false;
    bool typeSpecified = false;

    DWORD startType = SERVICE_DEMAND_START;
    bool delayedAuto = false;

    DWORD errorControl = SERVICE_ERROR_NORMAL;

    std::string binPath;
    std::string loadOrderGroup;

    bool tagSpecified = false;
    bool requestTag = false;

    std::string dependenciesRaw;
    std::string accountName = "LocalSystem";
    bool objSpecified = false;

    std::string displayName;
    std::string password;
    bool passwordSpecified = false;
};

// Print help text describing accepted `create` syntax and options.
static void PrintCreateUsage()
{
    std::cout << "DESCRIPTION:\n";
    std::cout << "        Creates a service entry in the registry and Service Database.\n\n";
    std::cout << "USAGE:\n";
    std::cout << "        sc_template create [service name] [option= value] ...\n\n";
    std::cout << "OPTIONS:\n";
    std::cout << "        type= {own | share | kernel | filesys | rec | interact}\n";
    std::cout << "        start= {boot | system | auto | demand | disabled | delayed-auto}\n";
    std::cout << "        error= {normal | severe | critical | ignore}\n";
    std::cout << "        binpath= <binary pathname>\n";
    std::cout << "        group= <load order group>\n";
    std::cout << "        tag= {yes | no}\n";
    std::cout << "        depend= <dependencies separated by />\n";
    std::cout << "        obj= <account name>\n";
    std::cout << "        displayname= <display name>\n";
    std::cout << "        password= <password>\n";
    std::cout << "        /?  Displays this help message.\n\n";
    std::cout << "REMARKS:\n";
    std::cout << "        Each command-line option must include the equal sign.\n";
    std::cout << "        A space is required between an option and its value.\n";
}

// Translate a Win32 error code into a human-readable system message.
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
}

// Recognize built-in service account names that do not require a password.
static bool IsBuiltInServiceAccount(const std::string& rawName)
{
    const std::string name = ToLower(rawName);

    return
        name == "localsystem" ||
        name == "localservice" ||
        name == "networkservice" ||
        name == "nt authority\\localsystem" ||
        name == "nt authority\\localservice" ||
        name == "nt authority\\networkservice" ||
        name == ".\\localsystem";
}

// Parse a `type=` value and update the target service-type flags, rejecting invalid combinations.
static bool TryParseTypeValue(CreateOptions& opts, const std::string& rawValue)
{
    const std::string value = ToLower(rawValue);

    if (value == "own")
    {
        if (opts.sawShare || opts.sawKernel || opts.sawFilesys || opts.sawRec)
            return false;

        opts.serviceType = SERVICE_WIN32_OWN_PROCESS;
        opts.sawOwn = true;
        opts.typeSpecified = true;
        return true;
    }

    if (value == "share")
    {
        if (opts.sawOwn || opts.sawKernel || opts.sawFilesys || opts.sawRec)
            return false;

        opts.serviceType = SERVICE_WIN32_SHARE_PROCESS;
        opts.sawShare = true;
        opts.typeSpecified = true;
        return true;
    }

    if (value == "kernel")
    {
        if (opts.sawOwn || opts.sawShare || opts.sawFilesys || opts.sawRec || opts.sawInteract)
            return false;

        opts.serviceType = SERVICE_KERNEL_DRIVER;
        opts.sawKernel = true;
        opts.typeSpecified = true;
        return true;
    }

    if (value == "filesys")
    {
        if (opts.sawOwn || opts.sawShare || opts.sawKernel || opts.sawRec || opts.sawInteract)
            return false;

        opts.serviceType = SERVICE_FILE_SYSTEM_DRIVER;
        opts.sawFilesys = true;
        opts.typeSpecified = true;
        return true;
    }

    if (value == "rec")
    {
        // Not supported by this emulator yet.
        return false;
    }

    if (value == "interact")
    {
        if (opts.sawKernel || opts.sawFilesys || opts.sawRec)
            return false;

        opts.sawInteract = true;
        opts.typeSpecified = true;
        return true;
    }

    return false;
}

// Parse a `start=` value into the matching SCM start-type constant.
static bool TryParseStartValue(CreateOptions& opts, const std::string& rawValue)
{
    const std::string value = ToLower(rawValue);

    opts.delayedAuto = false;

    if (value == "boot")
    {
        opts.startType = SERVICE_BOOT_START;
        return true;
    }

    if (value == "system")
    {
        opts.startType = SERVICE_SYSTEM_START;
        return true;
    }

    if (value == "auto")
    {
        opts.startType = SERVICE_AUTO_START;
        return true;
    }

    if (value == "demand")
    {
        opts.startType = SERVICE_DEMAND_START;
        return true;
    }

    if (value == "disabled")
    {
        opts.startType = SERVICE_DISABLED;
        return true;
    }

    if (value == "delayed-auto")
    {
        opts.startType = SERVICE_AUTO_START;
        opts.delayedAuto = true;
        return true;
    }

    return false;
}

// Parse an `error=` value into the matching SCM error-control constant.
static bool TryParseErrorValue(CreateOptions& opts, const std::string& rawValue)
{
    const std::string value = ToLower(rawValue);

    if (value == "normal")
    {
        opts.errorControl = SERVICE_ERROR_NORMAL;
        return true;
    }

    if (value == "severe")
    {
        opts.errorControl = SERVICE_ERROR_SEVERE;
        return true;
    }

    if (value == "critical")
    {
        opts.errorControl = SERVICE_ERROR_CRITICAL;
        return true;
    }

    if (value == "ignore")
    {
        opts.errorControl = SERVICE_ERROR_IGNORE;
        return true;
    }

    return false;
}

static bool TryParseTagValue(CreateOptions& opts, const std::string& rawValue)
{
    const std::string value = ToLower(rawValue);

    if (value == "yes")
    {
        opts.tagSpecified = true;
        opts.requestTag = true;
        return true;
    }

    if (value == "no")
    {
        opts.tagSpecified = true;
        opts.requestTag = false;
        return true;
    }

    return false;
}

// Convert raw `create` command tokens into a structured options object.
static bool ParseCreateArguments(const std::vector<std::string>& args, CreateOptions& opts)
{
    if (args.empty() || !EqualsIgnoreCase(args[0], "create"))
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

    std::vector<ParsedOption> parsedOptions;
    if (!ParseOptions(args, 2, parsedOptions))
    {
        return false;
    }

    for (const auto& opt : parsedOptions)
    {
        if (opt.key == "type")
        {
            if (!TryParseTypeValue(opts, opt.value))
            {
                return false;
            }
        }
        else if (opt.key == "start")
        {
            if (!TryParseStartValue(opts, opt.value))
            {
                return false;
            }
        }
        else if (opt.key == "error")
        {
            if (!TryParseErrorValue(opts, opt.value))
            {
                return false;
            }
        }
        else if (opt.key == "binpath")
        {
            opts.binPath = opt.value;
        }
        else if (opt.key == "group")
        {
            opts.loadOrderGroup = opt.value;
        }
        else if (opt.key == "tag")
        {
            if (!TryParseTagValue(opts, opt.value))
            {
                return false;
            }
        }
        else if (opt.key == "depend")
        {
            opts.dependenciesRaw = opt.value;
        }
        else if (opt.key == "obj")
        {
            opts.accountName = opt.value;
            opts.objSpecified = true;
        }
        else if (opt.key == "displayname")
        {
            opts.displayName = opt.value;
        }
        else if (opt.key == "password")
        {
            opts.password = opt.value;
            opts.passwordSpecified = true;
        }
        else
        {
            return false;
        }
    }

    if (opts.binPath.empty())
    {
        return false;
    }

    if (opts.sawInteract)
    {
        if (!(opts.sawOwn || opts.sawShare))
        {
            return false;
        }

        opts.serviceType |= SERVICE_INTERACTIVE_PROCESS;
    }

    if (opts.delayedAuto && opts.startType != SERVICE_AUTO_START)
    {
        return false;
    }

    if (opts.startType == SERVICE_BOOT_START || opts.startType == SERVICE_SYSTEM_START)
    {
        if (!(opts.sawKernel || opts.sawFilesys || opts.sawRec))
        {
            return false;
        }
    }

    if ((opts.sawKernel || opts.sawFilesys || opts.sawRec) && opts.objSpecified)
    {
        // For drivers, obj= refers to driver object name semantics. Keeping this
        // implementation simple and aligned to service creation path only.
        return false;
    }

    if (opts.sawInteract)
    {
        if (!IsBuiltInServiceAccount(opts.accountName) &&
            !EqualsIgnoreCase(opts.accountName, "LocalSystem") &&
            !EqualsIgnoreCase(opts.accountName, "NT AUTHORITY\\LocalSystem"))
        {
            return false;
        }
    }

    if (opts.objSpecified && !IsBuiltInServiceAccount(opts.accountName) && !opts.passwordSpecified)
    {
        return false;
    }

    return true;
}

static std::vector<char> BuildDependenciesMultiSz(const std::string& raw)
{
    std::vector<char> multiSz;

    if (raw.empty())
    {
        multiSz.push_back('\0');
        return multiSz;
    }

    size_t start = 0;
    while (start <= raw.size())
    {
        size_t slashPos = raw.find('/', start);
        std::string token;

        if (slashPos == std::string::npos)
        {
            token = raw.substr(start);
            start = raw.size() + 1;
        }
        else
        {
            token = raw.substr(start, slashPos - start);
            start = slashPos + 1;
        }

        if (!token.empty())
        {
            multiSz.insert(multiSz.end(), token.begin(), token.end());
            multiSz.push_back('\0');
        }
    }

    multiSz.push_back('\0');
    return multiSz;
}

static bool CreateServiceFromOptions(const CreateOptions& opts)
{
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE);
    if (scm == nullptr)
    {
        DWORD err = GetLastError();
        std::cout << "[SC] OpenSCManager FAILED " << err << ":\n";
        PrintWin32ErrorMessage(err);
        return false;
    }

    DWORD tagId = 0;
    DWORD* tagPtr = opts.requestTag ? &tagId : nullptr;

    std::vector<char> dependenciesMultiSz = BuildDependenciesMultiSz(opts.dependenciesRaw);

    LPCSTR displayNamePtr = opts.displayName.empty() ? nullptr : opts.displayName.c_str();
    LPCSTR groupPtr = opts.loadOrderGroup.empty() ? nullptr : opts.loadOrderGroup.c_str();
    LPCSTR depsPtr = opts.dependenciesRaw.empty() ? nullptr : dependenciesMultiSz.data();
    LPCSTR accountPtr = opts.objSpecified ? opts.accountName.c_str() : nullptr;
    LPCSTR passwordPtr = opts.passwordSpecified ? opts.password.c_str() : nullptr;

    SC_HANDLE service = CreateServiceA(
        scm,
        opts.serviceName.c_str(),
        displayNamePtr,
        SERVICE_ALL_ACCESS,
        opts.serviceType,
        opts.startType,
        opts.errorControl,
        opts.binPath.c_str(),
        groupPtr,
        tagPtr,
        depsPtr,
        accountPtr,
        passwordPtr);

    if (service == nullptr)
    {
        DWORD err = GetLastError();
        std::cout << "[SC] CreateService FAILED " << err << ":\n";
        PrintWin32ErrorMessage(err);
        CloseServiceHandle(scm);
        return false;
    }

    if (opts.delayedAuto)
    {
        SERVICE_DELAYED_AUTO_START_INFO delayedInfo{};
        delayedInfo.fDelayedAutostart = TRUE;

        if (!ChangeServiceConfig2A(
            service,
            SERVICE_CONFIG_DELAYED_AUTO_START_INFO,
            &delayedInfo))
        {
            DWORD err = GetLastError();

            // Roll back the newly created service so the command is atomic.
            DeleteService(service);

            std::cout << "[SC] ChangeServiceConfig2 FAILED " << err << ":\n";
            PrintWin32ErrorMessage(err);

            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            return false;
        }
    }

    std::cout << "[SC] CreateService SUCCESS\n";
    std::cout << "SERVICE_NAME: " << opts.serviceName << "\n";

    if (opts.requestTag)
    {
        std::cout << "TAG                 : " << tagId << "\n";
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}

// Validate inputs and create a new service entry using `CreateService`.
bool HandleCreateCommand(const std::vector<std::string>& args)
{
    CreateOptions opts;

    if (!ParseCreateArguments(args, opts))
    {
        PrintDefaultError();
        return false;
    }

    if (opts.showHelp)
    {
        PrintCreateUsage();
        return true;
    }

    return CreateServiceFromOptions(opts);
}