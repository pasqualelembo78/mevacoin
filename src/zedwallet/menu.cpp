// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2019, The Kryptokrona Developers
//
// Please see the included LICENSE file for more information.

///////////////////////////
#include <zedwallet/menu.h>
///////////////////////////

#include <common/signal_handler.h>

#include <utilities/coloured_msg.h>
#include <zedwallet/command_dispatcher.h>
#include <zedwallet/commands.h>
#include <zedwallet/get_input.h>
#include <zedwallet/sync.h>
#include <zedwallet/tools.h>

template <typename T>
std::string parseCommand(const std::vector<T> &printableCommands,
                         const std::vector<T> &availableCommands,
                         std::string prompt,
                         bool backgroundRefresh,
                         std::shared_ptr<WalletInfo> walletInfo)
{
    while (true)
    {
        /* Get the input, and refresh the wallet in the background if desired
           (This will be done on the main screen, but not the launch screen) */
        std::string selection = getInputAndWorkInBackground(
            availableCommands, prompt, backgroundRefresh, walletInfo);

        /* Convert to lower case */
        std::transform(selection.begin(), selection.end(), selection.begin(),
                       ::tolower);

        /* \n == no-op */
        if (selection == "")
        {
            continue;
        }

        try
        {
            int selectionNum = std::stoi(selection);

            /* Input is in 1 based indexing, we need 0 based indexing */
            selectionNum--;

            int numCommands = static_cast<int>(availableCommands.size());

            /* Must be in the bounds of the vector */
            if (selectionNum < 0 || selectionNum >= numCommands)
            {
                std::cout << WarningMsg("Bad input, expected a command name, ")
                          << WarningMsg("or number from ")
                          << InformationMsg("1")
                          << WarningMsg(" to ")
                          << InformationMsg(std::to_string(numCommands))
                          << std::endl;

                /* Print the available commands again if the input is bad */
                printCommands(printableCommands);

                continue;
            }

            selection = availableCommands[selectionNum].commandName;
        }
        /* Too lazy to dedupe this part, lol */
        catch (const std::out_of_range &)
        {
            int numCommands = static_cast<int>(availableCommands.size());

            std::cout << WarningMsg("Bad input, expected a command name, ")
                      << WarningMsg("or number from ")
                      << InformationMsg("1")
                      << WarningMsg(" to ")
                      << InformationMsg(std::to_string(numCommands))
                      << std::endl;

            /* Print the available commands again if the input is bad */
            printCommands(printableCommands);

            continue;
        }
        /* Input ain't a number */
        catch (const std::invalid_argument &)
        {
            /* Iterator pointing to the command, if it exists */
            auto it = std::find_if(availableCommands.begin(),
                                   availableCommands.end(),
                                   [&selection](const Command &c)
                                   {
                                       return c.commandName == selection;
                                   });

            /* Command doesn't exist in availableCommands */
            if (it == availableCommands.end())
            {
                std::cout << "Unknown command: " << WarningMsg(selection)
                          << std::endl;

                /* Print the available commands again if the input is bad */
                printCommands(printableCommands);

                continue;
            }
        }

        /* All good */
        return selection;
    }
}

std::tuple<bool, std::shared_ptr<WalletInfo>>
selectionScreen(Config &config, mevacoin::WalletGreen &wallet,
                mevacoin::INode &node)
{
    while (true)
    {
        /* Get the users action */
        std::string launchCommand = getAction(config);

        /* User wants to exit */
        if (launchCommand == "exit")
        {
            return {true, nullptr};
        }

        /* Handle the user input */
        std::shared_ptr<WalletInfo> walletInfo = handleLaunchCommand(
            wallet, launchCommand, config);

        /* Action failed, for example wallet file is corrupted. */
        if (walletInfo == nullptr)
        {
            std::cout << InformationMsg("Returning to selection screen...")
                      << std::endl;

            continue;
        }

        /* Node is down, user wants to exit */
        if (!checkNodeStatus(node))
        {
            return {true, nullptr};
        }

        /* If we're creating a wallet, don't print the lengthy sync process */
        if (launchCommand == "create")
        {
            std::stringstream str;

            str << std::endl
                << "Your wallet is syncing with the network in the background."
                << std::endl
                << "Until this is completed new transactions might not show "
                << "up." << std::endl
                << "Use the status command to check the progress."
                << std::endl;

            std::cout << InformationMsg(str.str());
        }
        else
        {
            /* Need another signal handler here, in case the user does
               ctrl+c whilst syncing, to save the wallet. The walletInfo
               ptr will be null in the parent scope, since we haven't returned
               it yet. */
            bool alreadyShuttingDown = false;

            tools::SignalHandler::install([&]
                                          {
                if (shutdown(walletInfo, node, alreadyShuttingDown))
                {
                    exit(0);
                } });

            syncWallet(node, walletInfo);
        }

        /* Return the wallet info */
        return {false, walletInfo};
    }
}

bool checkNodeStatus(mevacoin::INode &node)
{
    while (node.getLastKnownBlockHeight() == 0)
    {
        std::stringstream msg;

        msg << "It looks like " << wallet_config::daemonName << " isn't open!"
            << std::endl
            << std::endl
            << "Ensure " << wallet_config::daemonName
            << " is open and has finished initializing." << std::endl
            << "If it's still not working, try restarting "
            << wallet_config::daemonName << "."
            << "The daemon sometimes gets stuck."
            << std::endl
            << "Alternatively, perhaps "
            << wallet_config::daemonName << " can't communicate with any peers."
            << std::endl
            << std::endl
            << "The wallet can't function fully until it can communicate with "
            << "the network.";

        std::cout << WarningMsg(msg.str()) << std::endl;

        /* Print the commands */
        printCommands(nodeDownCommands());

        /* See what the user wants to do */
        std::string command = parseCommand(nodeDownCommands(),
                                           nodeDownCommands(),
                                           "What would you like to do?: ",
                                           false,
                                           nullptr);

        /* If they want to try again, check the node height again */
        if (command == "try_again")
        {
            continue;
        }
        /* If they want to exit, exit */
        else if (command == "exit")
        {
            return false;
        }
        /* If they want to continue, proceed to the menu screen */
        else if (command == "continue")
        {
            return true;
        }
    }

    return true;
}

std::string getAction(Config &config)
{
    if (config.walletGiven || config.passGiven)
    {
        return "open";
    }

    printCommands(startupCommands());

    return parseCommand(startupCommands(), startupCommands(),
                        "What would you like to do?: ", false, nullptr);
}

void mainLoop(std::shared_ptr<WalletInfo> walletInfo, mevacoin::INode &node)
{
    if (walletInfo->viewWallet)
    {
        printCommands(basicViewWalletCommands());
    }
    else
    {
        printCommands(basicCommands());
    }

    while (true)
    {
        std::string command;

        if (walletInfo->viewWallet)
        {
            command = parseCommand(
                basicViewWalletCommands(),
                allViewWalletCommands(),
                getPrompt(walletInfo),
                true,
                walletInfo);
        }
        else
        {
            command = parseCommand(
                basicCommands(),
                allCommands(),
                getPrompt(walletInfo),
                true,
                walletInfo);
        }

        /* User exited */
        if (!handleCommand(command, walletInfo, node))
        {
            return;
        }
    }
}

template <typename T>
void printCommands(const std::vector<T> &commands, size_t offset)
{
    size_t i = 1 + offset;

    std::cout << std::endl;

    for (const auto &command : commands)
    {
        std::cout << InformationMsg(" ")
                  << InformationMsg(std::to_string(i))
                  << "\t"
                  << SuccessMsg(command.commandName, 25) /* Pad to 25 chars */
                  << command.description << std::endl;

        i++;
    }

    std::cout << std::endl;
}

/* Template instantations that we are going to use - this allows us to have
   the template implementation in the .cpp file. */
template std::string parseCommand(const std::vector<Command> &printableCommands,
                                  const std::vector<Command> &availableCommands,
                                  std::string prompt,
                                  bool backgroundRefresh,
                                  std::shared_ptr<WalletInfo> walletInfo);

template std::string parseCommand(const std::vector<AdvancedCommand> &printableCommands,
                                  const std::vector<AdvancedCommand> &availableCommands,
                                  std::string prompt,
                                  bool backgroundRefresh,
                                  std::shared_ptr<WalletInfo> walletInfo);

template void printCommands(const std::vector<Command> &commands, size_t offset);

template void printCommands(const std::vector<AdvancedCommand> &commands, size_t offset);
