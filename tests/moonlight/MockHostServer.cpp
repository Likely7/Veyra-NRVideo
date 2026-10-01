// SPDX-License-Identifier: GPL-3.0-only
// The mock Sunshine host as a stand-alone process, for driving the real UI in an end-to-end check
// (scripts/moonlight/ui-demo.py). It prints "PORTS <http> <https>" and then serves until told to stop.
//   --pin-file <path>   the PIN the user would type on the host is read from this file (and the file
//                       deleted) whenever it appears
//   --stop-file <path>  exits when this file appears
//   --game <id>         the game reported as running (default 0)
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "MockHost.h"

using namespace veyra::moonlight::mock;

int main(int argc, char** argv) {
    std::string pinFile, stopFile;
    int game = 0;
    for (int i = 1; i + 1 < argc; ++i) {
        if (!std::strcmp(argv[i], "--pin-file")) pinFile = argv[++i];
        else if (!std::strcmp(argv[i], "--stop-file")) stopFile = argv[++i];
        else if (!std::strcmp(argv[i], "--game")) game = std::atoi(argv[++i]);
    }
    MockHost host("");
    host.currentGame = game;
    std::printf("PORTS %u %u\n", unsigned(host.httpPort), unsigned(host.httpsPort));
    std::fflush(stdout);
    namespace fs = std::filesystem;
    for (;;) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::error_code ec;
        if (!stopFile.empty() && fs::exists(stopFile, ec)) break;
        if (!pinFile.empty() && fs::exists(pinFile, ec)) {
            std::string pin;
            { std::ifstream in(pinFile); std::getline(in, pin); }
            while (!pin.empty() && (pin.back() == '\r' || pin.back() == ' ')) pin.pop_back();
            if (!pin.empty()) {
                fs::remove(pinFile, ec);
                host.setPin(pin);
                std::printf("PIN accepted\n");
                std::fflush(stdout);
            }
        }
    }
    return 0;
}
