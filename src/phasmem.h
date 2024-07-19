#pragma once

#if !(_WIN64 || __x86_64__)
#error "Expected a 64-bit target platform"
#endif

#include <array>
#include <filesystem>
#include <span>
#include <functional>

#include "il2cpp_rpm.h"

class PhasMem : protected Il2CppRPM
{
protected:
  /*
  The internal structure of Phasmo that we are after (after removing the BeeByte obfuscation):

    public class Network : MonoBehaviourPunCallbacks
    {
        public class PlayerSpot {
            // fields ...
            public Player player;
            // fields ...
            public string accountName;
            // fields ...
        };

        private static Network _instance;
        public Player localPlayer;
        public List<Network.PlayerSpot> playersData;
        // fields ...
    }

    public class Player : MonoBehaviour {
        // fields ...
        public PlayerAudio playerAudio;
        // fields ...
    };

    public class PlayerAudio : MonoBehaviour
    {
        // fields ...
        public WalkieTalkie walkieTalkie;
        // fields ...
    }

    public class WalkieTalkie : MonoBehaviour {
        // fields ...
        public bool isOn;
        // fields ...
        private bool isGhostSpawned;
        // fields ...
    };
  */

  /**
   * Cached offsets
   * Note:
   *  Caching field offsets would also be possible, however, we would still need to validate them, which almost costs
   *  the same as just finding them again dynamically.
   */
  struct CacheData {
    uintptr_t cls_Network{};    // Offset to a pointer to Network's class instance in GameAssembly.dll
    uintptr_t cls_PlayerSpot{}; // Offset to a pointer to PlayerSpot's class instance in GameAssembly.dll
  } m_cacheData;

  /**
   * Runtime information
   */
  struct DynData {
    uintptr_t fld_Network_localPlayer{};         // offsetof(Network, localPlayer)
    uintptr_t fld_Network_playersData{};         // offsetof(Network, playersData)
    uintptr_t fld_Player_playerAudio{};          // offsetof(Player, playerAudio)
    uintptr_t fld_PlayerAudio_walkieTalkie{};    // offsetof(PlayerAudio, walkieTalkie)
    uintptr_t fld_WalkieTalkie_isGhostSpawned{}; // offsetof(WalkieTalkie, isGhostSpawned)
    uintptr_t fld_PlayerSpot_player{};           // offsetof(PlayerSpot, player)
    uintptr_t fld_PlayerSpot_accountName{};      // offsetof(PlayerSpot, accountName)

    // NOTE: technically, we don't need to save the class instance pointers
    uintptr_t pcls_Network{};      // A pointer to Network's class instance
    uintptr_t pinst_Network{};     // A pointer to the static Network instance
    uintptr_t pcls_Player{};       // A pointer to Player's class instance
    uintptr_t pcls_PlayerAudio{};  // A pointer to PlayerAudio's class instance
    uintptr_t pcls_WalkieTalkie{}; // A pointer to WalkieTalkie's class instance
    uintptr_t pcls_PlayerSpot{};   // A pointer to PlayerSpot's class instance
  } m_dynData;

  bool m_inited = false;

  // Settings
  std::filesystem::path m_cachePath = std::filesystem::temp_directory_path() / "phasmo_global_vc_fixer.cache";
  bool m_shouldLoadCache = true;
  bool m_shouldSaveCache = true;

  /**
   * Tries to load the cache from the cache file.
   */
  bool loadCache();

  /**
   * Tries to save the cache to the cache file.
   */
  bool saveCache();

public:
  static constexpr WinRPM::PathViewType PHASMO_EXE_NAME = WINRPM_PATH("Phasmophobia.exe");
  static constexpr auto MAX_PLAYERS = 4;

  PhasMem() = default;
  ~PhasMem() { this->close(); }

  /**
   * Attempts to open Phasmophobia.exe and initialize everything (including finding the offsets).
   */
  Il2CppRPM::OpenResult open();

  /**
   * Closes the handle to the game and resets the internal state.
   */
  void close();

  /**
   * Tries to find the offsets and init the pointers from the game's memory.
   */
  bool init();

  /**
   * Returns whether there is an open handle to the game's process.
   */
  inline bool isOpen() const { return Il2CppRPM::isOpen(); }

  /**
   * Returns whether the offsets has been initialized.
   */
  inline bool isInited() const { return m_inited; }

  enum class WalkieTalkieFixState {
    ForceOff, // Sets the remote isGhostSpawned fields to true (forces the glitch to occur, for demonstration purposes)
    ForceOn,  // Sets the remote isGhostSpawned fields to false (for demonstration purposes)
    Auto      // Synchronizes the remote isGhostSpawned fields with the local player's
  };

  /**
   * Looks for glitched WalkieTalkie instances on the remote players and attempts to fix them.
   * The state parameter can be used to force a certain value for the remote players for demonstration purposes.
   * Returns true if there were no errors.
   */
  bool fixWalkieTalkies(WalkieTalkieFixState state = WalkieTalkieFixState::Auto);

  inline const std::filesystem::path& getCachePath() const { return m_cachePath; }
  inline void setCachePath(std::filesystem::path cachePath) { m_cachePath = std::move(cachePath); }

  inline bool shouldLoadCache() const { return m_shouldLoadCache; }
  inline void setShouldLoadCache(bool shouldLoadCache) { m_shouldLoadCache = shouldLoadCache; }

  inline bool shouldSaveCache() const { return m_shouldSaveCache; }
  inline void setShouldSaveCache(bool shouldSaveCache) { m_shouldSaveCache = shouldSaveCache; }

  using Il2CppRPM::isVerbose;
  using Il2CppRPM::setVerbose;
};