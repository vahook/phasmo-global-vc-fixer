// Sadly, C++20 lacks a proper UTF implementation
#define _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING
#include "phasmem.h"

#include <array>
#include <codecvt>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <span>
#include <thread>

#include "rpm.h"

// clang-format off
#define LOG_COUT(...) std::cout << __VA_ARGS__
#define LOG_COUTF(...) std::cout << std::format(__VA_ARGS__)
#define LOG_CERR(...) std::cerr << __VA_ARGS__
#define LOG_CERRF(...) std::cerr << std::format(__VA_ARGS__)
#define LOG_VERB(...) if (m_verbose) std::cerr << __VA_ARGS__
#define LOG_VERBF(...) if (m_verbose) std::cerr << std::format(__VA_ARGS__)
// clang-format on

bool PhasMem::loadCache()
{
  // Try to open the cache file
  std::ifstream is(m_cachePath, std::ios_base::in | std::ios_base::binary);
  if (!is) {
    LOG_CERRF("[Error]: Couldn't open cache file '{:s}' for reading.\n", m_cachePath.string());
    return false;
  }

  // Try to read the cache
  m_cacheData = {};
  if (!(is.read((char*)&m_cacheData, sizeof(m_cacheData)))) {
    LOG_CERRF("[Error]: Couldn't read cache file '{:s}'.\n", m_cachePath.string());
    return false;
  }

  LOG_CERRF("[Info]: Loaded offsets from cache file '{:s}'\n", m_cachePath.string());
  return true;
}

bool PhasMem::saveCache()
{
  // Try to open the cache file
  std::ofstream os(m_cachePath, std::ios_base::out | std::ios_base::binary);
  if (!os) {
    LOG_CERRF("[Warning]: Couldn't open cache file '{:s}' for writing\n", m_cachePath.string());
    return false;
  }

  // Try to write the offset
  if (!(os.write((char*)&m_cacheData, sizeof(m_cacheData)))) {
    LOG_CERRF("[Warning]: Couldn't write cache file '{:s}'\n", m_cachePath.string());
    return false;
  }

  LOG_CERRF("[Info]: Saved offsets to cache file '{:s}'\n", m_cachePath.string());
  return true;
}

Il2CppRPM::OpenResult PhasMem::open()
{
  return Il2CppRPM::open(PHASMO_EXE_NAME);
}

void PhasMem::close()
{
  Il2CppRPM::close();
  m_inited = false;
  m_cacheData = {};
  m_dynData = {};
}

struct PEVirtualSection {
  uintptr_t offset{}; // VirtualAddress
  uintptr_t size{};   // SizeOfRawData
};

/**
 * Tries to find the VirtualAddress and SizeOfRawData fields of a PE section based on the section's name.
 * Upon failure, the offset will be set to 0.
 */
static PEVirtualSection findPEVirtualSection(void* peFileBase, std::string_view sectionName)
{
  /**
   * Windows types that we need.
   * We need to declare these anyway for the linux version, and while we are at it, we may also use it for the Windows
   * version as well. This way we don't have to include the entirety of Windows.h
   */

  constexpr uint16_t win_IMAGE_DOS_SIGNATURE = 0x5A4D;    // MZ
  constexpr uint32_t win_IMAGE_NT_SIGNATURE = 0x00004550; // PE00

  struct win_IMAGE_DOS_HEADER {
    uint16_t e_magic;
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    int32_t e_lfanew;
  };

  struct win_IMAGE_FILE_HEADER {
    uint16_t Machine;
    uint16_t NumberOfSections;
    uint32_t TimeDateStamp;
    uint32_t PointerToSymbolTable;
    uint32_t NumberOfSymbols;
    uint16_t SizeOfOptionalHeader;
    uint16_t Characteristics;
  };

  struct win_IMAGE_NT_HEADERS64 {
    uint32_t Signature;
    win_IMAGE_FILE_HEADER FileHeader;
    uint8_t OptionalHeader[1];
  };

  constexpr auto win_IMAGE_SIZEOF_SHORT_NAME = 8;

  struct win_IMAGE_SECTION_HEADER {
    uint8_t Name[win_IMAGE_SIZEOF_SHORT_NAME];

    union {
      uint32_t PhysicalAddress;
      uint32_t VirtualSize;
    } Misc;

    uint32_t VirtualAddress;
    uint32_t SizeOfRawData;
    uint32_t PointerToRawData;
    uint32_t PointerToRelocations;
    uint32_t PointerToLinenumbers;
    uint16_t NumberOfRelocations;
    uint16_t NumberOfLinenumbers;
    uint32_t Characteristics;
  };

  if (sectionName.size() > win_IMAGE_SIZEOF_SHORT_NAME)
    return {};

  const auto dosHeader = (const win_IMAGE_DOS_HEADER*)peFileBase;
  if (dosHeader->e_magic != win_IMAGE_DOS_SIGNATURE)
    return {};
  const auto peHeader = (const win_IMAGE_NT_HEADERS64*)((uintptr_t)dosHeader + dosHeader->e_lfanew);
  if (peHeader->Signature != win_IMAGE_NT_SIGNATURE)
    return {};

  auto sectionHeader =
    (const win_IMAGE_SECTION_HEADER*)((uintptr_t)peHeader + offsetof(win_IMAGE_NT_HEADERS64, OptionalHeader) +
                                      peHeader->FileHeader.SizeOfOptionalHeader);
  for (int i = 0; i < peHeader->FileHeader.NumberOfSections; ++i, ++sectionHeader) {
    if (::memcmp(sectionHeader->Name, sectionName.data(), sectionName.size()) == 0) {
      return {sectionHeader->VirtualAddress, sectionHeader->SizeOfRawData};
    }
  }
  return {};
}

bool PhasMem::init()
{
  // Reinit
  m_inited = false;

  // We need the the RPM interface open
  if (!this->isOpen()) {
    LOG_CERR("[Error]: Remote Process Interface is not open.\n");
    return false;
  }

  // -----------------------------
  // - Network and PlayerSpot class file offsets
  // -----------------------------

  const auto checkCachedClass = [&](std::string_view className, uintptr_t& cacheField, uintptr_t& dynField) {
    if (cacheField) {
      if (m_rpm.read(m_gameAssemblyBase, dynField, cacheField) &&
          this->il2cpp_class_hasNameAndNamespace(dynField, {className, ""})) {
        LOG_VERBF("[Debug]: Found {:s}'s class offset in the cache.\n", className);
      } else {
        LOG_VERBF("[Debug]: The cached {:s} class offset was invalid.\n", className);
        cacheField = 0;
        dynField = 0;
      }
    }
  };

  // Try the cache
  m_cacheData = {};
  if (m_shouldLoadCache && this->loadCache()) {
    checkCachedClass("Network", m_cacheData.cls_Network, m_dynData.pcls_Network);
    checkCachedClass("PlayerSpot", m_cacheData.cls_PlayerSpot, m_dynData.pcls_PlayerSpot);
  }

  // Did we manage to find everything in the cache?
  const bool wasCacheValid = m_cacheData.cls_Network && m_cacheData.cls_PlayerSpot;
  if (wasCacheValid) {
    // If the cache is valid, that means we've already resolved the class instances too.
    LOG_CERR("[Info]: Cache was fully valid, skipping .data section scanning.\n");
  } else {
    if (m_shouldLoadCache)
      LOG_CERR("[Info]: Couldn't find every offset in the cache.\n");
    LOG_CERR("[Info]: Scanning .data section.\n");

    // Read and parse the PE header of GameAssembly.dll from the memory
    std::array<char, 0x1000> headerBuffer;
    if (!m_rpm.read(m_gameAssemblyBase, headerBuffer)) {
      LOG_VERBF("[Error]: Couldn't read DOS/PE header.\n");
      return false;
    }

    // Find .data
    const auto dataSec = findPEVirtualSection(headerBuffer.data(), ".data");
    if (dataSec.offset == 0) {
      LOG_VERBF("[Error]: Couldn't find .data section.\n");
      return false;
    }

    {
      // Read the entire .data section (~6-7 MB)
      auto dataSegBuffer = std::make_unique<unsigned char[]>(dataSec.size);
      if (!m_rpm.read_raw(m_gameAssemblyBase + dataSec.offset, dataSegBuffer.get(), dataSec.size)) {
        LOG_VERBF("[Error]: Couldn't read .data section.\n");
        return false;
      }

      // Scan the .data section
      m_cacheData.cls_Network = 0;
      m_cacheData.cls_PlayerSpot = 0;
      for (uintptr_t offset = 0; offset < dataSec.size && (!m_cacheData.cls_Network || !m_cacheData.cls_PlayerSpot);
           offset += 8) {
        const uintptr_t instPtr = *(uintptr_t*)&dataSegBuffer[offset];
        Il2CppId classId;
        if (!this->il2cpp_class_heuristicCheck(instPtr, classId))
          continue;

        if (!m_cacheData.cls_Network && classId.equal("Network", "")) {
          m_cacheData.cls_Network = dataSec.offset + offset;
          m_dynData.pcls_Network = instPtr;
        } else if (!m_cacheData.cls_PlayerSpot && classId.equal("PlayerSpot", "")) {
          m_cacheData.cls_PlayerSpot = dataSec.offset + offset;
          m_dynData.pcls_PlayerSpot = instPtr;
        }
      }
    }
  }

  // Checking the class instances is not needed, since either the cache is fully valid, or the invalid entries are
  // freshly resolved from the .data section.
#define CHECK_CACHED_CLASS_INITED(CLASS_NAME, CACHE_FIELD, DYN_FIELD)                                            \
  LOG_VERBF(                                                                                                     \
    "[Debug]: [" CLASS_NAME " class offset: {:#016x}, " CLASS_NAME " class instance: {:#016x}].\n", CACHE_FIELD, \
    DYN_FIELD                                                                                                    \
  );                                                                                                             \
  if (!CACHE_FIELD) {                                                                                            \
    LOG_VERBF("[Error]: Couldn't find " CLASS_NAME "'s class offset.\n");                                        \
    return false;                                                                                                \
  }

  CHECK_CACHED_CLASS_INITED("Network", m_cacheData.cls_Network, m_dynData.pcls_Network)
  CHECK_CACHED_CLASS_INITED("PlayerSpot", m_cacheData.cls_PlayerSpot, m_dynData.pcls_PlayerSpot)

#undef CHECK_CACHED_CLASS_INITED

  // Optionally, cache the data
  if (!wasCacheValid && m_shouldSaveCache)
    this->saveCache();

  // Grab the static Network instance (it should be the first static field)
  if (!m_rpm.read(m_dynData.pcls_Network, m_dynData.pinst_Network, offsetof(il2cpp::Il2CppClass, static_fields), 0) ||
      this->il2cpp_obj_getClassInstance(m_dynData.pinst_Network) != m_dynData.pcls_Network) {
    LOG_VERBF("[Error]: Couldn't resolve Network's static instance.\n");
    return false;
  }

  LOG_VERBF("[Debug]: [Network instance: {:#016x}]\n", m_dynData.pinst_Network);

  // -----------------------------
  // - Field helpers
  // -----------------------------

#define CHECK_FIELD_INITED(FIELD_NAME, FIELD_VAR)                        \
  LOG_VERBF("[Debug]: [" FIELD_NAME " offset: {:#016x}].\n", FIELD_VAR); \
  if (FIELD_VAR == 0) {                                                  \
    LOG_VERB("[Error]: Couldn't find the offset of " FIELD_NAME " .\n"); \
    return false;                                                        \
  }

  // -----------------------------
  // - Network field offsets
  // -----------------------------

  // Look for Network.localPlayer and Network.playersData
  m_dynData.fld_Network_localPlayer = 0;
  m_dynData.fld_Network_playersData = 0;
  this->il2cpp_class_enumFields(m_dynData.pcls_Network, [&](const il2cpp::FieldInfo& field) -> bool {
    il2cpp::Il2CppType type;
    if (!m_rpm.read(field.type, type))
      return false;

    // Network.localPlayer (type: Player)
    if (type.type == il2cpp::Il2CppTypeEnum::IL2CPP_TYPE_CLASS) {
      if (m_dynData.fld_Network_localPlayer)
        goto nextIter;
      if (this->il2cpp_typedef_hasNameAndNamespace(type.data, {"Player", ""}))
        m_dynData.fld_Network_localPlayer = field.offset;
    }

    // Network.playersData (type: System.Collections.Generic.List<Network.PlayerSpot>)
    else if (type.type == il2cpp::Il2CppTypeEnum::IL2CPP_TYPE_GENERICINST) {
      if (m_dynData.fld_Network_playersData)
        goto nextIter;

      il2cpp::Il2CppGenericClass genericClass;
      if (!m_rpm.read(type.data, genericClass))
        return false;
      il2cpp::Il2CppType genericType;
      if (!m_rpm.read(genericClass.type, genericType))
        return false;

      if (genericType.type != il2cpp::Il2CppTypeEnum::IL2CPP_TYPE_CLASS)
        goto nextIter;
      if (!this->il2cpp_typedef_hasNameAndNamespace(genericType.data, {"List`1", "System.Collections.Generic"}))
        goto nextIter;

      il2cpp::Il2CppGenericInst genericInstance;
      if (!m_rpm.read(genericClass.context.class_inst, genericInstance))
        return false;

      // Might be redundant due to the suffix in the name
      if (genericInstance.type_argc != 1)
        goto nextIter;

      il2cpp::Il2CppType firstGenericType;
      if (!m_rpm.read(genericInstance.type_argv, firstGenericType, 0, 0))
        return false;

      if (firstGenericType.type != il2cpp::Il2CppTypeEnum::IL2CPP_TYPE_CLASS)
        goto nextIter;

      if (this->il2cpp_typedef_hasNameAndNamespace(firstGenericType.data, {"PlayerSpot", ""}))
        m_dynData.fld_Network_playersData = field.offset;
    }

nextIter:
    // Loop if we haven't found everything
    return !m_dynData.fld_Network_localPlayer || !m_dynData.fld_Network_playersData;
  });

  CHECK_FIELD_INITED("Network.localPlayer", m_dynData.fld_Network_localPlayer);
  CHECK_FIELD_INITED("Network.playersData", m_dynData.fld_Network_playersData);

  // -----------------------------
  // - Player field offsets
  // -----------------------------

  // Note: Network.localPlayer will also be valid in singleplayer
  uintptr_t localPlayer;
  if (!m_rpm.read(m_dynData.pinst_Network, localPlayer, m_dynData.fld_Network_localPlayer)) {
    LOG_VERB("[Error]: Couldn't read Network.localPlayer .\n");
    return false;
  }
  m_dynData.pcls_Player = this->il2cpp_obj_getClassInstance(localPlayer);

  m_dynData.fld_Player_playerAudio = 0;
  this->il2cpp_class_enumFields(m_dynData.pcls_Player, [&](const il2cpp::FieldInfo& field) -> bool {
    il2cpp::Il2CppType type;
    if (!m_rpm.read(field.type, type))
      return false;

    // Player.playerAudio (type: PlayerAudio)
    if (type.type == il2cpp::Il2CppTypeEnum::IL2CPP_TYPE_CLASS &&
        this->il2cpp_typedef_hasNameAndNamespace(type.data, {"PlayerAudio", ""})) {
      m_dynData.fld_Player_playerAudio = field.offset;
      return false;
    }
    return true;
  });

  CHECK_FIELD_INITED("Player.playerAudio", m_dynData.fld_Player_playerAudio);

  // -----------------------------
  // - PlayerAudio field offsets
  // -----------------------------

  uintptr_t playerAudio;
  if (!m_rpm.read(localPlayer, playerAudio, m_dynData.fld_Player_playerAudio)) {
    LOG_VERB("[Error]: Couldn't read Player.playerAudio .\n");
    return false;
  }
  m_dynData.pcls_PlayerAudio = this->il2cpp_obj_getClassInstance(playerAudio);

  m_dynData.fld_PlayerAudio_walkieTalkie = 0;
  this->il2cpp_class_enumFields(m_dynData.pcls_PlayerAudio, [&](const il2cpp::FieldInfo& field) -> bool {
    il2cpp::Il2CppType type;
    if (!m_rpm.read(field.type, type))
      return false;

    // PlayerAudio.walkieTalkie (type: WalkieTalkie)
    if (type.type == il2cpp::Il2CppTypeEnum::IL2CPP_TYPE_CLASS &&
        this->il2cpp_typedef_hasNameAndNamespace(type.data, {"WalkieTalkie", ""})) {
      m_dynData.fld_PlayerAudio_walkieTalkie = field.offset;
      return false;
    }
    return true;
  });

  CHECK_FIELD_INITED("PlayerAudio.walkieTalkie", m_dynData.fld_PlayerAudio_walkieTalkie);

  // -----------------------------
  // - WalkieTalkie field offsets
  // -----------------------------

  uintptr_t walkieTalkie;
  if (!m_rpm.read(playerAudio, walkieTalkie, m_dynData.fld_PlayerAudio_walkieTalkie)) {
    LOG_VERB("[Error]: Couldn't read PlayerAudio.walkieTalkie .\n");
    return false;
  }
  m_dynData.pcls_WalkieTalkie = this->il2cpp_obj_getClassInstance(walkieTalkie);

  m_dynData.fld_WalkieTalkie_isGhostSpawned = 0;
  this->il2cpp_class_enumFields(m_dynData.pcls_WalkieTalkie, [&](const il2cpp::FieldInfo& field) -> bool {
    il2cpp::Il2CppType type;
    if (!m_rpm.read(field.type, type))
      return false;

    // WalkieTalkie.isGhostSpawned (type: bool)
    //  This one has an obfuscated name, and the class holds 2 booleans: isOn and isGhostSpawned.
    //  However, isOn is public, meanwhile isGhostSpawned is private .
    if (type.type == il2cpp::Il2CppTypeEnum::IL2CPP_TYPE_BOOLEAN && type.attrs == 1) {
      m_dynData.fld_WalkieTalkie_isGhostSpawned = field.offset;
      return false;
    }
    return true;
  });

  CHECK_FIELD_INITED("WalkieTalkie.isGhostSpawned", m_dynData.fld_WalkieTalkie_isGhostSpawned);

  // -----------------------------
  // - PlayerSpot field offsets
  // -----------------------------

  m_dynData.fld_PlayerSpot_player = 0;
  m_dynData.fld_PlayerSpot_accountName = 0;
  this->il2cpp_class_enumFields(m_dynData.pcls_PlayerSpot, [&](const il2cpp::FieldInfo& field) -> bool {
    il2cpp::Il2CppType type;
    if (!m_rpm.read(field.type, type))
      return false;

    const bool typeClass = type.type == il2cpp::Il2CppTypeEnum::IL2CPP_TYPE_CLASS;
    const bool typeString = type.type == il2cpp::Il2CppTypeEnum::IL2CPP_TYPE_STRING;
    if (!typeClass && !typeString)
      return true;

    // The field names in this class are not obfuscated, so we may rely on them
    const auto fieldName = this->meta_remoteStrToLocal(field.name);
    if (!fieldName)
      return false;

    // PlayerSpot.player (type: Player)
    if (!m_dynData.fld_PlayerSpot_player && typeClass && fieldName == "player" &&
        this->il2cpp_typedef_hasNameAndNamespace(type.data, {"Player", ""})) {
      m_dynData.fld_PlayerSpot_player = field.offset;
    }

    // PlayerSpot.accountName (type: string)
    else if (!m_dynData.fld_PlayerSpot_accountName && typeString && fieldName == "accountName") {
      m_dynData.fld_PlayerSpot_accountName = field.offset;
    }

    return !m_dynData.fld_PlayerSpot_player || !m_dynData.fld_PlayerSpot_accountName;
  });

  CHECK_FIELD_INITED("PlayerSpot.player", m_dynData.fld_PlayerSpot_player);
  CHECK_FIELD_INITED("PlayerSpot.accountName", m_dynData.fld_PlayerSpot_accountName);

#undef CHECK_FIELD_INITED

  m_inited = true;
  return true;
}

bool PhasMem::fixWalkieTalkies(WalkieTalkieFixState state)
{
  if (!this->isOpen()) {
    LOG_VERB("[Error]: Not opened.\n");
    return false;
  }
  if (!this->isInited()) {
    LOG_VERB("[Error]: Not initialized.\n");
    return false;
  }

  // Get the networked players
  il2cpp::System_Collections_Generic_List playersDataList;
  if (!m_rpm.read(m_dynData.pinst_Network, playersDataList, m_dynData.fld_Network_playersData, 0)) {
    LOG_VERB("[Error]: Couldn't read Network.playersData .\n");
    return false;
  }

  // The size will be 0 in singleplayer
  if (playersDataList.size == 0)
    return true;
  if (playersDataList.size < 0 || playersDataList.size > MAX_PLAYERS) {
    LOG_VERBF("[Error]: Invalid Network.playersData.Count (got: {}).\n", playersDataList.size);
    return false;
  }
  std::array<uintptr_t, MAX_PLAYERS> playerDataArray;
  if (!m_rpm.read_raw(
        playersDataList.items + offsetof(il2cpp::Il2CppArray, items), playerDataArray.data(),
        playersDataList.size * sizeof(uintptr_t)
      )) {
    LOG_VERB("[Error]: Couldn't read Network.playersData elements.\n");
    return false;
  }

  // Get the local player, so we can compare its WalkieTalkie's isGhostSpawned field (the ground truth) with others
  uintptr_t localPlayer;
  if (!m_rpm.read(m_dynData.pinst_Network, localPlayer, m_dynData.fld_Network_localPlayer)) {
    LOG_VERB("[Error]: Couldn't read Network.localPlayer .\n");
    return false;
  }

  bool localIsGhostSpawned;
  if (!m_rpm.read(
        localPlayer, localIsGhostSpawned, m_dynData.fld_Player_playerAudio, m_dynData.fld_PlayerAudio_walkieTalkie,
        m_dynData.fld_WalkieTalkie_isGhostSpawned
      )) {
    LOG_VERB("[Error]: Couldn't read Network.localPlayer.playerAudio.walkieTalkie.isGhostSpawned .\n");
    return false;
  }

  // Enumerate the networked players
  for (int i = 0; i < playersDataList.size; ++i) {
    const auto playerSpotPtr = playerDataArray[i];

    uintptr_t player;
    if (!m_rpm.read(playerSpotPtr, player, m_dynData.fld_PlayerSpot_player)) {
      LOG_VERB("[Error]: Couldn't read Network.playersData[i].player .\n");
      return false;
    }

    // Skip the local player (we could also just skip the first element of the list)
    if (player == localPlayer)
      continue;

    // Get the remote player's WalkieTalkie's isGhostSpawned field
    uintptr_t walkieTalkie;
    if (!m_rpm.read(player, walkieTalkie, m_dynData.fld_Player_playerAudio, m_dynData.fld_PlayerAudio_walkieTalkie)) {
      LOG_VERB("[Error]: Couldn't read Network.playersData[i].player.playerAudio.walkieTalkie .\n");
      return false;
    }

    bool isGhostSpawned;
    if (!m_rpm.read(walkieTalkie, isGhostSpawned, m_dynData.fld_WalkieTalkie_isGhostSpawned)) {
      LOG_VERB("[Error]: Couldn't read Network.playersData[i].player.playerAudio.walkieTalkie.isGhostSpawned .\n");
      return false;
    }

    // Determine the new value to be written back
    bool newIsGhostSpawned;
    switch (state) {
    case WalkieTalkieFixState::ForceOff:
      newIsGhostSpawned = false;
      break;
    case WalkieTalkieFixState::ForceOn:
      newIsGhostSpawned = true;
      break;
    case WalkieTalkieFixState::Auto:
      newIsGhostSpawned = localIsGhostSpawned;
    }

    // Don't do unnecessary memory writes, and just continue if there is nothing to do
    if (isGhostSpawned == newIsGhostSpawned)
      continue;

    // FIXME: Windows might not be able to display UTF-8 strings properly in the console.
    std::string accoundNameStr;
    uintptr_t accountNamePtr;
    if (!m_rpm.read(playerSpotPtr + m_dynData.fld_PlayerSpot_accountName, accountNamePtr) ||
        !this->il2cpp_string_readUTF8(accountNamePtr, accoundNameStr)) {
      LOG_VERB("[Error]: Couldn't read Network.playersData[i].accountName .\n");
      // Default to no name
      accoundNameStr.clear();
    }

    // Write back the new value
    // NOTE: technically, this *could* crash the game, if somehow the WalkieTalkie object gets destroyed and garbage
    //  collected just before this memory write.
    if (!m_rpm.write(walkieTalkie, newIsGhostSpawned, m_dynData.fld_WalkieTalkie_isGhostSpawned)) {
      LOG_VERB("[Error]: Couldn't write to Network.playersData[i].player.playerAudio.walkieTalkie.isGhostSpawned .\n");
      LOG_CERRF("[Error]: Couldn't fix the walkie-talkie of remote player (idx: {}): '{}'.\n", i, accoundNameStr);
      return false;
    }

    // Log that we've applied the fix
    LOG_CERRF("[Info]: Fixed the walkie-talkie of remote player (idx: {}): '{}'\n", i, accoundNameStr);
    LOG_VERBF(
      "[Debug]: [remote isGhostSpawned: {} -> {}, local isGhostSpawned: {}]\n", isGhostSpawned, newIsGhostSpawned,
      localIsGhostSpawned
    );
  }

  return true;
}