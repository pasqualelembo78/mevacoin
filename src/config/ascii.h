// Copyright (c) 2018-2019, The TurtleCoin Developers
// Copyright (c) 2024, MevaCoin Project.
//
// Please see the included LICENSE file for more information.

#pragma once

const std::string windowsAsciiArt =
    "\n  __  __                  _____      _       \n"
    " |  \\/  |                / ____|    (_)      \n"
    " | \\  / | _____   ____ _| |     ___  _ _ __  \n"
    " | |\\/| |/ _ \\ \\ / / _` | |    / _ \\| | '_ \\ \n"
    " | |  | |  __/\\ V / (_| | |___| (_) | | | | |\n"
    " |_|  |_|\\___| \\_/ \\__,_|\\_____|\\___/|_|_| |_|\n"
    "                                              \n"
    " 🎉 Block initialized on Desy's Birthday: 18 December 2011 🎉 \n";

const std::string nonWindowsAsciiArt =
    "\n  __  __                  _____      _       \n"
    " |  \\/  |                / ____|    (_)      \n"
    " | \\  / | _____   ____ _| |     ___  _ _ __  \n"
    " | |\\/| |/ _ \\ \\ / / _` | |    / _ \\| | '_ \\ \n"
    " | |  | |  __/\\ V / (_| | |___| (_) | | | | |\n"
    " |_|  |_|\\___| \\_/ \\__,_|\\_____|\\___/|_|_| |_|\n"
    "                                              \n"
    " 🎉 Block initialized on Desy's Birthday: 18 December 2011 🎉 \n";

/* Windows has some characters it won't display in a terminal.
   If your ascii art works fine on both Windows and Linux terminals,
   you can replace 'asciiArt' directly with the art itself and
   remove the #ifdefs and the above ascii arts */
#ifdef _WIN32
const std::string asciiArt = windowsAsciiArt;
#else
const std::string asciiArt = nonWindowsAsciiArt;
#endif