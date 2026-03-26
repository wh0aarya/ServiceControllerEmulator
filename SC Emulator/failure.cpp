#include "failure.h"
#include "utils.h"

#include <windows.h>
#include <winsvc.h>

#include <cctype>
#include <iostream>
#include <string>
#include <vector>

struct FailureActionItem
{
    SC_ACTION_TYPE type = SC_ACTION_NONE;
    DWORD delayMs = 0;
};

struct FailureOptions
{
    bool showHelp = false;
    std::string serviceName;

    bool resetSpecified = false;
    DWORD resetPeriodSeconds = 0;

    bool rebootSpecified = false;
    std::string rebootMessage;

    bool commandSpecified = false;
    std::string commandLine;

    bool actionsSpecified = false;
    std::vector<FailureActionItem> actions;
};

static void PrintFailureUsage()
{
    std::cout << "DESCRIPTION:\n";
    std::cout << "        Changes the failure actions of a service.\n\n";

    std::cout << "USAGE:\n";
    std::cout << "        sc_template failure [service name] [option= value] ...\n\n";

    std::cout << "OPTIONS:\n";
    std::cout << "        reset= <seconds>\n";
    std::cout << "        reboot= <message>\n";
    std::cout << "        command= <command line>\n";
    std::cout << "        actions= {\"\" | {none/<ms> | run/<ms> | restart/<ms> | reboot/<ms>}[/...]}\n";
    std::cout << "        /?  Displays this help message.\n\n";

    std::cout << "REMARKS:\n";
    std::cout << "        reset= requires actions=, and actions= requires reset=.\n";
    std::cout << "        Each option must include the equal sign.\n";
    std::cout << "        A space is required between an option and its value.\n";
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

static std::vector<std::string> SplitString(const std::string& text, char delimiter)
{
    std::vector<std::string> parts;
    std::string current;

    for (char c : text)
    {
        if (c == delimiter)
        {
            parts.push_back(current);
            current.clear();
        }
        else
        {
            current.push_back(c);
        }
    }

    parts.push_back(current);
    return parts;
}

static bool TryParseSingleFailureAction(
    const std::string& actionText,
    const std::string& delayText,
    FailureActionItem& out)
{
    const std::string action = ToLower(actionText);
    DWORD delay = 0;

    if (!TryParseDwordStrict(delayText, delay))
    {
        return false;
    }

    if (action == "run")
    {
        out.type = SC_ACTION_RUN_COMMAND;
        out.delayMs = delay;
        return true;
    }

    if (action == "restart")
    {
        out.type = SC_ACTION_RESTART;
        out.delayMs = delay;
        return true;
    }

    if (action == "reboot")
    {
        out.type = SC_ACTION_REBOOT;
        out.delayMs = delay;
        return true;
    }

    if (action == "none")
    {
        out.type = SC_ACTION_NONE;
        out.delayMs = delay;
        return true;
    }

    // Match observed sc.exe leniency more closely:
    // unknown action names are treated as NONE instead of failing parse.
    out.type = SC_ACTION_NONE;
    out.delayMs = delay;
    return true;
}

static bool TryParseActionsValue(
    const std::string& rawValue,
    std::vector<FailureActionItem>& actionsOut)
{
    std::string value = rawValue;

    if (value == "\"\"")
    {
        value.clear();
    }

    actionsOut.clear();

    if (value.empty())
    {
        return true;
    }

    std::vector<std::string> parts = SplitString(value, '/');

    if (parts.empty() || (parts.size() % 2) != 0)
    {
        return false;
    }

    const size_t actionCount = parts.size() / 2;
    if (actionCount == 0)
    {
        return false;
    }

    for (size_t i = 0; i < parts.size(); i += 2)
    {
        FailureActionItem item;
        if (!TryParseSingleFailureAction(parts[i], parts[i + 1], item))
        {
            return false;
        }

        actionsOut.push_back(item);
    }

    return true;
}

static const char* FailureActionTypeToString(SC_ACTION_TYPE type)
{
    switch (type)
    {
    case SC_ACTION_NONE:
        return "NONE";
    case SC_ACTION_RESTART:
        return "RESTART";
    case SC_ACTION_REBOOT:
        return "REBOOT";
    case SC_ACTION_RUN_COMMAND:
        return "RUN COMMAND";
    default:
        return "UNKNOWN";
    }
}

static bool EnableShutdownPrivilege()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
    {
        return false;
    }

    LUID luid = {};
    if (!LookupPrivilegeValueA(nullptr, "SeShutdownPrivilege", &luid))
    {
        CloseHandle(token);
        return false;
    }

    TOKEN_PRIVILEGES tp = {};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    if (!AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr))
    {
        CloseHandle(token);
        return false;
    }

    DWORD err = GetLastError();
    CloseHandle(token);
    return err == ERROR_SUCCESS;
}

static bool ParseFailureArguments(
    const std::vector<std::string>& args,
    FailureOptions& opts)
{
    if (args.empty() || !EqualsIgnoreCase(args[0], "failure"))
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

    for (const ParsedOption& opt : parsedOptions)
    {
        if (opt.key == "reset")
        {
            if (opts.resetSpecified)
            {
                return false;
            }

            DWORD value = 0;
            if (!TryParseDwordStrict(opt.value, value))
            {
                return false;
            }

            opts.resetSpecified = true;
            opts.resetPeriodSeconds = value;
        }
        else if (opt.key == "reboot")
        {
            if (opts.rebootSpecified)
            {
                return false;
            }

            opts.rebootSpecified = true;
            opts.rebootMessage = opt.value;
        }
        else if (opt.key == "command")
        {
            if (opts.commandSpecified)
            {
                return false;
            }

            opts.commandSpecified = true;
            opts.commandLine = opt.value;
        }
        else if (opt.key == "actions")
        {
            if (opts.actionsSpecified)
            {
                return false;
            }

            if (!TryParseActionsValue(opt.value, opts.actions))
            {
                return false;
            }

            opts.actionsSpecified = true;
        }
        else
        {
            return false;
        }
    }

    if (opts.resetSpecified != opts.actionsSpecified)
    {
        return false;
    }

    if (!opts.resetSpecified || !opts.actionsSpecified)
    {
        return false;
    }

    return true;
}

static bool ChangeFailureActions(const FailureOptions& opts)
{
    SC_HANDLE scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr)
    {
        DWORD err = GetLastError();
        std::cout << "[SC] OpenSCManager FAILED " << err << ":\n";
        PrintWin32ErrorMessage(err);
        return false;
    }

    // SERVICE_START is important when failure actions include RESTART.
    // SERVICE_QUERY_CONFIG is useful for parity/debugging and does not hurt.
    const DWORD desiredAccess =
        SERVICE_CHANGE_CONFIG | SERVICE_START | SERVICE_QUERY_CONFIG;

    SC_HANDLE service = OpenServiceA(scm, opts.serviceName.c_str(), desiredAccess);
    if (service == nullptr)
    {
        DWORD err = GetLastError();
        std::cout << "[SC] OpenService FAILED " << err << ":\n";
        PrintWin32ErrorMessage(err);
        CloseServiceHandle(scm);
        return false;
    }

    std::vector<SC_ACTION> scActions;
    bool needsRebootPrivilege = false;

    for (const FailureActionItem& item : opts.actions)
    {
        SC_ACTION action = {};
        action.Type = item.type;
        action.Delay = item.delayMs;
        scActions.push_back(action);

        if (item.type == SC_ACTION_REBOOT)
        {
            needsRebootPrivilege = true;
        }
    }

    if (needsRebootPrivilege && !EnableShutdownPrivilege())
    {
        std::cout << "[SC] Failed to enable shutdown privilege.\n";
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_FAILURE_ACTIONSA sfa = {};
    sfa.dwResetPeriod = opts.resetPeriodSeconds;
    sfa.lpRebootMsg = opts.rebootSpecified ? const_cast<LPSTR>(opts.rebootMessage.c_str()) : nullptr;
    sfa.lpCommand = opts.commandSpecified ? const_cast<LPSTR>(opts.commandLine.c_str()) : nullptr;
    sfa.cActions = static_cast<DWORD>(scActions.size());
    sfa.lpsaActions = scActions.empty() ? nullptr : scActions.data();

    if (!ChangeServiceConfig2A(service, SERVICE_CONFIG_FAILURE_ACTIONS, &sfa))
    {
        DWORD err = GetLastError();
        std::cout << "[SC] ChangeServiceConfig2 FAILED " << err << ":\n";
        PrintWin32ErrorMessage(err);
        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return false;
    }

    std::cout << "[SC] ChangeServiceConfig2 SUCCESS\n";
    std::cout << "SERVICE_NAME: " << opts.serviceName << "\n";
    std::cout << "        RESET_PERIOD (in seconds)    : " << opts.resetPeriodSeconds << "\n";
    std::cout << "        REBOOT_MESSAGE               : "
        << (opts.rebootSpecified ? opts.rebootMessage : "") << "\n";
    std::cout << "        COMMAND_LINE                 : "
        << (opts.commandSpecified ? opts.commandLine : "") << "\n";

    std::cout << "        FAILURE_ACTIONS              : ";
    if (scActions.empty())
    {
        std::cout << "NONE\n";
    }
    else
    {
        for (size_t i = 0; i < scActions.size(); ++i)
        {
            if (i != 0)
            {
                std::cout << "                                       ";
            }

            std::cout << FailureActionTypeToString(scActions[i].Type)
                << " -- Delay = " << scActions[i].Delay
                << " milliseconds.\n";
        }
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);
    return true;
}

bool HandleFailureCommand(const std::vector<std::string>& args)
{
    FailureOptions opts;

    if (!ParseFailureArguments(args, opts))
    {
        PrintDefaultError();
        return false;
    }

    if (opts.showHelp)
    {
        PrintFailureUsage();
        return true;
    }

    std::cout << "Parsed command:\n";
    std::cout << "failure\n";
    std::cout << "Parsed service name:\n";
    std::cout << opts.serviceName << "\n";
    std::cout << "Parsed options:\n";

    if (opts.resetSpecified)
    {
        std::cout << "reset= " << opts.resetPeriodSeconds << "\n";
    }

    if (opts.rebootSpecified)
    {
        std::cout << "reboot= " << opts.rebootMessage << "\n";
    }

    if (opts.commandSpecified)
    {
        std::cout << "command= " << opts.commandLine << "\n";
    }

    if (opts.actionsSpecified)
    {
        std::cout << "actions= ";
        if (opts.actions.empty())
        {
            std::cout << "\"\"";
        }
        else
        {
            for (size_t i = 0; i < opts.actions.size(); ++i)
            {
                std::string actionName;
                if (opts.actions[i].type == SC_ACTION_RUN_COMMAND) actionName = "run";
                else if (opts.actions[i].type == SC_ACTION_RESTART) actionName = "restart";
                else if (opts.actions[i].type == SC_ACTION_REBOOT) actionName = "reboot";
                else actionName = "none";

                std::cout << actionName << "/" << opts.actions[i].delayMs;
                if (i + 1 < opts.actions.size())
                {
                    std::cout << "/";
                }
            }
        }
        std::cout << "\n";
    }

    std::cout << "-------------------------\n";

    return ChangeFailureActions(opts);
}