/**
 * sctest.cpp
 *
 * Entry point for the SC.exe emulator.
 *
 * Responsibilities:
 * - Parse command-line arguments
 * - Determine which command is being invoked
 * - Dispatch execution to the appropriate handler
 */

#include "utils.h"
#include "query.h"
#include "qc.h"
#include "create.h"
#include "qdescription.h"
#include "start.h"
#include "stop.h"
#include "delete.h"
#include "config.h"
#include "failure.h"
#include "qfailure.h"

#include <iostream>
#include <string>
#include <vector>
#include <Windows.h>

 /**
  * Converts a subset of std::string arguments into std::wstring.
  *
  * This is required because some Windows API functions expect wide strings.
  *
  * @param args         Full argument list
  * @param startIndex   Index to start converting from
  * @return vector of wide strings
  */


static std::wstring WideFromUtf8(const std::string& value)
{
    if (value.empty())
    {
        return std::wstring();
    }

    int sizeNeeded = MultiByteToWideChar(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0);

    if (sizeNeeded <= 0)
    {
        return std::wstring();
    }

    std::wstring result(sizeNeeded, L'\0');

    MultiByteToWideChar(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        &result[0],
        sizeNeeded);

    return result;
}

static std::vector<std::wstring> ConvertArgsToWide(
    const std::vector<std::string>& args,
    size_t startIndex)
{
    std::vector<std::wstring> wideArgs;
    wideArgs.reserve(args.size() - startIndex);

    for (size_t i = startIndex; i < args.size(); ++i)
    {
        wideArgs.push_back(WideFromUtf8(args[i]));
    }

    return wideArgs;
}

/**
 * Program entry point.
 */
int main(int argc, char* argv[])
{
    // Ensure at least one command is provided
    if (argc < 2)
    {
        PrintUsage();
        return 1;
    }

    // Convert argv into a cleaner vector<string>
    std::vector<std::string> args;
    args.reserve(static_cast<size_t>(argc - 1));

    for (int i = 1; i < argc; ++i)
    {
        args.emplace_back(argv[i]);
    }

    // Normalize command to lowercase for case-insensitive comparison
    const std::string command = ToLower(args[0]);

    // Dispatch to the appropriate handler
    if (command == "query")
        return HandleQueryCommand(args) ? 0 : 1;

    if (command == "qc")
        return HandleQcCommand(args) ? 0 : 1;

    if (command == "create")
        return HandleCreateCommand(args) ? 0 : 1;

    if (command == "qdescription")
        return HandleQDescriptionCommand(args) ? 0 : 1;

    if (command == "start")
        return HandleStartCommand(ConvertArgsToWide(args, 1));

    if (command == "stop")
        return HandleStopCommand(ConvertArgsToWide(args, 1));

    if (command == "delete")
        return HandleDeleteCommand(ConvertArgsToWide(args, 1));

    if (command == "config")
        return HandleConfigCommand(args) ? 0 : 1;

    if (command == "failure")
        return HandleFailureCommand(args) ? 0 : 1;

    if (command == "qfailure")
        return HandleQFailureCommand(args) ? 0 : 1;

    // If command is not recognized, show default SC-style error
    PrintDefaultError();
    return 1;
}