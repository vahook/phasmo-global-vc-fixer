#include <iostream>
#include <filesystem>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

#if __linux__
#include <csignal>
#elif _WIN32
#define _AMD64_
#include <consoleapi.h>
#endif

#include "phasmem.h"

static PhasMem g_phasMem;

static void printHelp(const char* argv0)
{
  const auto progName = std::filesystem::path{argv0}.filename().string();
  // clang-format off
  std::cout
    << "Usage: " << progName << " [OPTIONS]...\n"
       "  -h, --help           print this message and exit\n"
       "  -v, --verbose        print extended debug messages\n"
       "  -s, --singleshot     do not run in a loop, quit after one fix attempt\n"
       "  --dont-load-cache    bypass the cache and resolve the offsets directly from the game's memory\n"
       "  --dont-save-cache    don't save the offsets to cache\n"
       "  --force [1/0]        force the isGhostSpawned flag to either true or false (for demonstration purposes)\n";
  // clang-format on
}

// Stupid cooperative multithreading hack so that we can nicely exit using CTRL+C
static std::mutex g_shutdownMtx;
static std::condition_variable g_shutdownCv;
static bool g_shutdown = false;
static void handleShutdown()
{
  std::cout << "[Info]: Got CTRL-C, shutting down...\n";
  {
    std::lock_guard shutdownLock{g_shutdownMtx};
    g_shutdown = true;
    g_shutdownCv.notify_all();
  }
}

template <typename T>
inline static bool waitForShutdown(const T& delay)
{
  std::unique_lock shutdownLock{g_shutdownMtx};
  return (g_shutdownCv.wait_for(shutdownLock, delay, []() { return g_shutdown; }));
}

int main(int argc, const char* argv[])
{
  std::cout << "--- Phasmophobia global voice chat fixer ---\n";

  // --------------------
  // - Arguments
  // --------------------

  bool verbose = false;
  bool singleshot = false;
  bool sholdLoadCache = true;
  bool sholdSaveCache = true;
  PhasMem::WalkieTalkieFixState fixState = PhasMem::WalkieTalkieFixState::Auto;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "-h" || arg == "--help") {
      printHelp(argv[0]);
      return 0;
    } else if (arg == "-v" || arg == "--verbose") {
      verbose = true;
    } else if (arg == "-s" || arg == "--singleshot") {
      singleshot = true;
    } else if (arg == "--dont-load-cache") {
      sholdLoadCache = false;
    } else if (arg == "--dont-save-cache") {
      sholdSaveCache = false;
    } else if (arg == "--force") {
      if (i + 1 >= argc) {
        std::cerr << "Not enough arguments for --force\n";
        printHelp(argv[0]);
        return 1;
      }
      const std::string_view argNext{argv[++i]};
      if (argNext == "0") {
        fixState = PhasMem::WalkieTalkieFixState::ForceOff;
      } else if (argNext == "1") {
        fixState = PhasMem::WalkieTalkieFixState::ForceOn;
      } else {
        std::cerr << "Invalid argument '" << argNext << "' for --force\n";
        printHelp(argv[0]);
        return 1;
      }
    } else {
      std::cerr << "Invalid argument '" << arg << "'\n";
      printHelp(argv[0]);
      return 1;
    }
  }

  g_phasMem.setVerbose(verbose);
  g_phasMem.setShouldLoadCache(sholdLoadCache);
  g_phasMem.setShouldSaveCache(sholdSaveCache);

  // --------------------
  // - Main
  // --------------------

  // Settings
  constexpr std::chrono::milliseconds openRetryDelay{5000};
  constexpr std::chrono::milliseconds initRetryDelay{5000};
  constexpr std::chrono::milliseconds fixDelay{5000};
  constexpr auto maxInitAttempts = 30;

  // Set CTRL-C handler
#if __linux__
  ::signal(SIGINT, [](int s) { handleShutdown(); });
#elif _WIN32
  ::SetConsoleCtrlHandler([](DWORD c) -> int { return (c == CTRL_C_EVENT) ? (handleShutdown(), true) : false; }, true);
#endif

  // Open phasmo
  {
    auto openStatus = g_phasMem.open();
    if (!singleshot) {
      // Wait for Phasmophobia
      while (openStatus == Il2CppRPM::OpenResult::NotFound) {
        std::cout << "[Info]: Waiting for Phasmophobia. Retrying in " << openRetryDelay << "\n";
        if (waitForShutdown(openRetryDelay))
          return 0;

        // Try again
        openStatus = g_phasMem.open();
      }
    }

    // Did we succeed?
    switch (openStatus) {
    case Il2CppRPM::OpenResult::Ok:
      break;
    case Il2CppRPM::OpenResult::NoPrivileges:
      std::cout << "[Error]: Didn't have permission to open Phasmophobia.\n";
      return 1;
    default:
      std::cout << "[Error]: Error while trying to open Phasmophobia.\n";
      return 1;
    }
  }

  // Init phasmo
  {
    g_phasMem.init();
    const auto maxAttempt = singleshot ? 1 : maxInitAttempts;
    for (int attempt = 1; !g_phasMem.isInited();) {
      std::cout << "[Error]: Couldn't initialize Phasmophobia offsets (attempt " << attempt << "/" << maxAttempt
                << ").\n";

      // Out of attempts
      if (++attempt > maxAttempt)
        return 1;

      // Wait a little, Phasmo might haven't been initialized yet
      std::cout << "[Info]: Retrying in " << initRetryDelay << "\n";
      if (waitForShutdown(initRetryDelay))
        return 0;

      // Try again
      g_phasMem.init();
    }
  }

  // Fix loop
  {
    const auto pulseFix = [&]() {
      if (!g_phasMem.fixWalkieTalkies(fixState) && g_phasMem.isOpen())
        std::cout << "[Error]: Encountered an error while trying to apply the fix.\n";
    };

    if (singleshot) {
      pulseFix();
    } else {
      std::cout << "[Info]: Running in loop mode. Press CTRL-C to quit.\n";
      for (;;) {
        // Pulse the fix
        pulseFix();

        // Exit if the game gets closed
        if (!g_phasMem.isOpen()) {
          std::cout << "[Info]: Phasmophobia was closed. Shutting down...\n";
          break;
        }

        // Wait a little
        if (waitForShutdown(fixDelay))
          return 0;
      };
    }
  }

  return 0;
}
