/**
 * utils.cpp
 *
 * Implementation of shared utility functions used across commands.
 */

#include "utils.h"

#include <iostream>
#include <algorithm>
#include <cctype>

 /**
  * Converts a string to lowercase.
  */
std::string ToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

/**
 * Case-insensitive string comparison.
 */
bool EqualsIgnoreCase(const std::string& a, const std::string& b)
{
    return ToLower(a) == ToLower(b);
}

/**
 * Prints a default SC-style error message.
 */
void PrintDefaultError()
{
    std::cout << "[SC] ERROR 87:\n";
    std::cout << "The parameter is incorrect.\n";
}

/**
 * Displays usage examples.
 */
void PrintUsage()
{
    std::cout << "Usage examples:\n";
    std::cout << "  sc_template query MyService\n";
    std::cout << "  sc_template qc MyService\n";
    std::cout << "  sc_template create MyService binPath= C:\\\\test.exe start= auto\n";
    std::cout << "  sc_template start MyService\n";
    std::cout << "  sc_template stop MyService\n";
}

/**
 * Checks if a command is valid.
 */
bool IsKnownCommand(const std::string& cmd)
{
    static const std::vector<std::string> commands = {
        "query", "qc", "create", "qdescription",
        "start", "stop", "delete", "config",
        "failure", "qfailure"
    };

    for (const auto& c : commands)
    {
        if (EqualsIgnoreCase(cmd, c))
            return true;
    }

    return false;
}

/**
 * Determines if a token is an option (ends with '=').
 */
bool IsOptionToken(const std::string& token)
{
    return !token.empty() && token.back() == '=';
}

/**
 * Parses command-line options into structured key-value pairs.
 */
bool ParseOptions(
    const std::vector<std::string>& rawArgs,
    size_t startIndex,
    std::vector<ParsedOption>& parsedOptions)
{
    parsedOptions.clear();

    for (size_t i = startIndex; i < rawArgs.size(); ++i)
    {
        const std::string& token = rawArgs[i];

        // Must be of form "key="
        if (!IsOptionToken(token))
            return false;

        // Must have a corresponding value
        if (i + 1 >= rawArgs.size())
            return false;

        ParsedOption opt;
        opt.key = ToLower(token.substr(0, token.size() - 1));
        opt.value = rawArgs[i + 1];

        parsedOptions.push_back(opt);
        ++i; // Skip value
    }

    return true;
}

bool HasOption(const std::vector<ParsedOption>& options, const std::string& key)
{
    const std::string wanted = ToLower(key);
    for (const auto& option : options)
    {
        if (ToLower(option.key) == wanted)
        {
            return true;
        }
    }
    return false;
}

std::string GetOptionValue(const std::vector<ParsedOption>& options, const std::string& key)
{
    const std::string wanted = ToLower(key);
    for (const auto& option : options)
    {
        if (ToLower(option.key) == wanted)
        {
            return option.value;
        }
    }
    return "";
}

bool ValidateCommandArguments(
    const std::string& command,
    const std::string& serviceName,
    const std::vector<ParsedOption>& options)
{
    (void)command;
    (void)options;
    return !serviceName.empty();
}

void PrintParsedArguments(
    const std::string& command,
    const std::string& serviceName,
    const std::vector<ParsedOption>& options)
{
    std::cout << "Parsed command:\n" << command << "\n";
    std::cout << "Parsed service name:\n" << serviceName << "\n";
    std::cout << "Parsed options:\n";

    if (options.empty())
    {
        std::cout << "(none)\n";
    }
    else
    {
        for (const auto& option : options)
        {
            std::cout << option.key << "= " << option.value << "\n";
        }
    }

    std::cout << "-------------------------\n";
}