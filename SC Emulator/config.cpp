// config.cpp
// Implements the `config` command, which updates an existing service's configuration in the SCM database.

#include "config.h"
#include "utils.h"

#include <windows.h>
#include <winsvc.h>

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>

#ifndef SERVICE_INTERACTIVE_PROCESS
#define SERVICE_INTERACTIVE_PROCESS 0x00000100
#endif

#ifndef SERVICE_ADAPTER
#define SERVICE_ADAPTER 0x00000004
#endif

#ifndef SERVICE_RECOGNIZER_DRIVER
#define SERVICE_RECOGNIZER_DRIVER 0x00000008
#endif

// Aggregate used to store parsed and normalized command-line options for this command.
struct ConfigOptions
{
    bool showHelp = false;
    std::string serviceName;

    bool sawAnyConfigOption = false;

    DWORD serviceType = SERVICE_NO_CHANGE;
    bool typeSpecified = false;

    bool sawOwn = false;
    bool sawShare = false;
    bool sawKernel = false;
    bool sawFilesys = false;
    bool sawRec = false;
    bool sawAdapt = false;
    bool sawInteract = false;

    DWORD startType = SERVICE_NO_CHANGE;
    bool startSpecified = false;
    bool delayedAutoSpecified = false;
    bool delayedAutoEnabled = false;

    DWORD errorControl = SERVICE_NO_CHANGE;
    bool errorSpecified = false;

    std::string binPath;
    bool binPathSpecified = false;

    std::string loadOrderGroup;
    bool groupSpecified = false;

    bool tagSpecified = false;
    bool requestTag = false;

    std::string dependenciesRaw;
    bool dependSpecified = false;

    std::string accountName;
    bool objSpecified = false;

    std::string displayName;
    bool displayNameSpecified = false;

    std::string password;
    bool passwordSpecified = false;
};

// Print help text describing accepted `config` syntax and options.
static void PrintConfigUsage()
{
    std::cout << "DESCRIPTION:\n";
    std::cout << "        Modifies the configuration of a service.\n\n";
    std::cout << "USAGE:\n";
    std::cout << "        sc_template config [service name] [option= value] ...\n\n";
    std::cout << "OPTIONS:\n";
    std::cout << "        type= {own | share | kernel | filesys | rec | adapt | interact}\n";
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
    else
    {
        std::cout << "\n";
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
static bool TryParseTypeValue(ConfigOptions& opts, const std::string& rawValue)
{
    const std::string value = ToLower(rawValue);

    if (value == "own")
    {
        if (opts.sawShare || opts.sawKernel || opts.sawFilesys || opts.sawRec || opts.sawAdapt)
            return false;

        opts.serviceType = SERVICE_WIN32_OWN_PROCESS;
        opts.sawOwn = true;
        opts.typeSpecified = true;
        return true;
    }

    if (value == "share")
    {
        if (opts.sawOwn || opts.sawKernel || opts.sawFilesys || opts.sawRec || opts.sawAdapt)
            return false;

        opts.serviceType = SERVICE_WIN32_SHARE_PROCESS;
        opts.sawShare = true;
        opts.typeSpecified = true;
        return true;
    }

    if (value == "kernel")
    {
        if (opts.sawOwn || opts.sawShare || opts.sawFilesys || opts.sawRec || opts.sawAdapt || opts.sawInteract)
            return false;

        opts.serviceType = SERVICE_KERNEL_DRIVER;
        opts.sawKernel = true;
        opts.typeSpecified = true;
        return true;
    }

    if (value == "filesys")
    {
        if (opts.sawOwn || opts.sawShare || opts.sawKernel || opts.sawRec || opts.sawAdapt || opts.sawInteract)
            return false;

        opts.serviceType = SERVICE_FILE_SYSTEM_DRIVER;
        opts.sawFilesys = true;
        opts.typeSpecified = true;
        return true;
    }

    if (value == "rec")
    {
        if (opts.sawOwn || opts.sawShare || opts.sawKernel || opts.sawFilesys || opts.sawAdapt || opts.sawInteract)
            return false;

        opts.serviceType = SERVICE_RECOGNIZER_DRIVER;
        opts.sawRec = true;
        opts.typeSpecified = true;
        return true;
    }

    if (value == "adapt")
    {
        if (opts.sawOwn || opts.sawShare || opts.sawKernel || opts.sawFilesys || opts.sawRec || opts.sawInteract)
            return false;

        opts.serviceType = SERVICE_ADAPTER;
        opts.sawAdapt = true;
        opts.typeSpecified = true;
        return true;
    }

    if (value == "interact")
    {
        if (opts.sawKernel || opts.sawFilesys || opts.sawRec || opts.sawAdapt)
            return false;

        opts.sawInteract = true;
        opts.typeSpecified = true;
        return true;
    }

    return false;
}

// Parse a `start=` value into the matching SCM start-type constant.
static bool TryParseStartValue(ConfigOptions& opts, const std::string& rawValue)
{
    const std::string value = ToLower(rawValue);

    if (value == "boot")
    {
        opts.startType = SERVICE_BOOT_START;
        opts.startSpecified = true;
        opts.delayedAutoSpecified = false;
        return true;
    }

    if (value == "system")
    {
        opts.startType = SERVICE_SYSTEM_START;
        opts.startSpecified = true;
        opts.delayedAutoSpecified = false;
        return true;
    }

    if (value == "auto")
    {
        opts.startType = SERVICE_AUTO_START;
        opts.startSpecified = true;
        opts.delayedAutoSpecified = true;
        opts.delayedAutoEnabled = false;
        return true;
    }

    if (value == "demand")
    {
        opts.startType = SERVICE_DEMAND_START;
        opts.startSpecified = true;
        opts.delayedAutoSpecified = false;
        return true;
    }

    if (value == "disabled")
    {
        opts.startType = SERVICE_DISABLED;
        opts.startSpecified = true;
        opts.delayedAutoSpecified = false;
        return true;
    }

    if (value == "delayed-auto")
    {
        opts.startType = SERVICE_AUTO_START;
        opts.startSpecified = true;
        opts.delayedAutoSpecified = true;
        opts.delayedAutoEnabled = true;
        return true;
    }

    return false;
}

// Parse an `error=` value into the matching SCM error-control constant.
static bool TryParseErrorValue(ConfigOptions& opts, const std::string& rawValue)
{
    const std::string value = ToLower(rawValue);

    if (value == "normal")
    {
        opts.errorControl = SERVICE_ERROR_NORMAL;
        opts.errorSpecified = true;
        return true;
    }

    if (value == "severe")
    {
        opts.errorControl = SERVICE_ERROR_SEVERE;
        opts.errorSpecified = true;
        return true;
    }

    if (value == "critical")
    {
        opts.errorControl = SERVICE_ERROR_CRITICAL;
        opts.errorSpecified = true;
        return true;
    }

    if (value == "ignore")
    {
        opts.errorControl = SERVICE_ERROR_IGNORE;
        opts.errorSpecified = true;
        return true;
    }

    return false;
}

static bool TryParseTagValue(ConfigOptions& opts, const std::string& rawValue)
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

static std::vector<char> BuildDependenciesMultiSz(const std::string& raw)
{
    std::vector<char> multiSz;

    if (raw.empty())
    {
        multiSz.push_back('\0');
        multiSz.push_back('\0');
        return multiSz;
    }

    size_t start = 0;
    while (start <= raw.size())
    {
        const size_t slashPos = raw.find('/', start);
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

// Convert raw `config` command tokens into a structured options object.
static bool ParseConfigArguments(const std::vector<std::string>& args, ConfigOptions& opts)
{
    if (args.empty() || !EqualsIgnoreCase(args[0], "config"))
    {
        return false;
    }

    if (args.size() >= 2 && args[1] == "/?")
    {
        opts.showHelp = true;
        return true;
    }

    if (args.size() < 4)
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

    if (parsedOptions.empty())
    {
        return false;
    }

    for (const auto& opt : parsedOptions)
    {
        opts.sawAnyConfigOption = true;

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
            opts.binPathSpecified = true;
        }
        else if (opt.key == "group")
        {
            opts.loadOrderGroup = opt.value;
            opts.groupSpecified = true;
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
            opts.dependSpecified = true;
        }
        else if (opt.key == "obj")
        {
            opts.accountName = opt.value;
            opts.objSpecified = true;
        }
        else if (opt.key == "displayname")
        {
            opts.displayName = opt.value;
            opts.displayNameSpecified = true;
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

    if (!opts.sawAnyConfigOption)
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

    if ((opts.startType == SERVICE_BOOT_START || opts.startType == SERVICE_SYSTEM_START) &&
        !(opts.sawKernel || opts.sawFilesys || opts.sawRec || opts.sawAdapt))
    {
        return false;
    }

    if ((opts.sawKernel || opts.sawFilesys || opts.sawRec || opts.sawAdapt) && opts.sawInteract)
    {
        return false;
    }

    if ((opts.sawKernel || opts.sawFilesys || opts.sawRec || opts.sawAdapt) && opts.objSpecified)
    {
        return false;
    }

    if (opts.sawInteract)
    {
        if (!IsBuiltInServiceAccount(opts.accountName.empty() ? "LocalSystem" : opts.accountName))
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

static bool ApplyConfig(const ConfigOptions& opts)
{
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr)
    {
        DWORD err = GetLastError();
        std::cout << "[SC] OpenSCManager FAILED " << err << ":\n";
        PrintWin32ErrorMessage(err);
        return false;
    }

    SC_HANDLE service = OpenServiceA(
        scm,
        opts.serviceName.c_str(),
        SERVICE_CHANGE_CONFIG | SERVICE_QUERY_CONFIG);
    if (service == nullptr)
    {
        DWORD err = GetLastError();
        std::cout << "[SC] OpenService FAILED " << err << ":\n";
        PrintWin32ErrorMessage(err);
        CloseServiceHandle(scm);
        return false;
    }

    std::vector<char> dependenciesMultiSz;
    LPCSTR dependenciesPtr = nullptr;
    if (opts.dependSpecified)
    {
        dependenciesMultiSz = BuildDependenciesMultiSz(opts.dependenciesRaw);
        dependenciesPtr = dependenciesMultiSz.data();
    }

    LPCSTR binPathPtr = opts.binPathSpecified ? opts.binPath.c_str() : nullptr;
    LPCSTR groupPtr = opts.groupSpecified ? opts.loadOrderGroup.c_str() : nullptr;
    LPCSTR accountPtr = opts.objSpecified ? opts.accountName.c_str() : nullptr;
    LPCSTR displayNamePtr = opts.displayNameSpecified ? opts.displayName.c_str() : nullptr;
    LPCSTR passwordPtr = opts.passwordSpecified ? opts.password.c_str() : nullptr;

    DWORD tagId = 0;
    DWORD* tagPtr = opts.tagSpecified && opts.requestTag ? &tagId : nullptr;

    if (!ChangeServiceConfigA(
        service,
        opts.typeSpecified ? opts.serviceType : SERVICE_NO_CHANGE,
        opts.startSpecified ? opts.startType : SERVICE_NO_CHANGE,
        opts.errorSpecified ? opts.errorControl : SERVICE_NO_CHANGE,
        binPathPtr,
        groupPtr,
        tagPtr,
        dependenciesPtr,
        accountPtr,
        passwordPtr,
        displayNamePtr))
    {
        DWORD err = GetLastError();
        std::cout << "[SC] ChangeServiceConfig FAILED " << err << ":\n";
        PrintWin32ErrorMessage(err);
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return false;
    }

    if (opts.delayedAutoSpecified)
    {
        SERVICE_DELAYED_AUTO_START_INFO delayedInfo{};
        delayedInfo.fDelayedAutostart = opts.delayedAutoEnabled ? TRUE : FALSE;

        if (!ChangeServiceConfig2A(
            service,
            SERVICE_CONFIG_DELAYED_AUTO_START_INFO,
            &delayedInfo))
        {
            DWORD err = GetLastError();
            std::cout << "[SC] ChangeServiceConfig2 FAILED " << err << ":\n";
            PrintWin32ErrorMessage(err);
            CloseServiceHandle(service);
            CloseServiceHandle(scm);
            return false;
        }
    }

    std::cout << "[SC] ChangeServiceConfig SUCCESS\n";
    std::cout << "SERVICE_NAME: " << opts.serviceName << "\n";

    if (opts.tagSpecified && opts.requestTag)
    {
        std::cout << "TAG                 : " << tagId << "\n";
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}

// Validate inputs, open the service, and apply the requested configuration changes.
bool HandleConfigCommand(const std::vector<std::string>& args)
{
    ConfigOptions opts;

    if (!ParseConfigArguments(args, opts))
    {
        PrintDefaultError();
        return false;
    }

    if (opts.showHelp)
    {
        PrintConfigUsage();
        return true;
    }

    return ApplyConfig(opts);
}