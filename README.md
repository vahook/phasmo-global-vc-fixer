# Global voice chat fixer for Phasmophobia

Phasmophobia has an incredibly frustrating and long-standing bug: sometimes the global voice chat just randomly breaks down, and you hear everyone as if they were talking locally, even though you can hear their walkie-talkies hissing. Well, if you've been suffering from this too, then I have some good news: I went through the hassle of reverse engineering Phasmophobia (breaking BeeByte obfuscator along the way), just to understand and fix this annoying issue.

Just as a disclaimer: I've already reported my findings through the discord modmail back in September 2023; however, nothing came of it. After the initial acknowledgement, I received no further updates or requests for clarification. Shortly afterwards, due to the lack of content updates, I stopped playing Phasmo and completely forgot about this project. But then the Eventide update rolled around and I picked up the game again. Sadly, however, on my very first run on Point Hope, I immediately ran into the same bug again.

I'm aware that the devs are not very fond of modding, but since they *still* haven't fixed this, and I'm seeing an awful lot of bug reports and complaints about this on discord, I've decided to make my fix available to the public. 
Even though I haven't been banned for using it: **I'm not taking any responsibility for anyone getting banned as a result of using this tool**. It's important to mention, that the program **does not inject code into the game**, it just **externally** flips 3 bits in the game's memory at most (1 per remote player). For those interested, later down in this README, you can read about [the underlying cause of the bug](#the-underlying-cause).

## Using the fix

**Important to clarify: the fix (just like the bug itself) is CLIENT-SIDE, meaning this program can only fix the walkie-talkies of REMOTE PLAYERS on YOUR END. If your friend also suffers from this bug, they will also have to run it on their end.**

The program is a simple console application that you can simply run as is. I made two versions: one for native Windows (`phasmo_global_vc_fixer.exe`), and another one for native Linux (`phasmo_global_vc_fixer`). You can find both in the [release section](https://github.com/vahook/phasmo-global-vc-fixer/releases/latest) (or optionally, check [how you can built it yourself](#building)). The latter one uses the `procfs` to interact with the game's memory, so make sure you have ptrace capabilities. But it's also possible to use the Windows version inside Phasmo's proton prefix (for example by using [protontricks](https://github.com/Matoking/protontricks): `protontricks-launch --appid 739630 --no-bwrap phasmo_global_vc_fixer.exe`).

### Loop mode

By default, the program will scan Phasmo's memory every few seconds looking for glitched walkie-talkies to fix.

### Singleshot mode

By using the `-s` (or `--singleshot`) flag, you can have it quit after just one fix attempt. This is useful if you want to apply the fix manually whenever you see the bug occouring rather than having the program constantly running in the background. In this mode, `-q` (or `--quick-exit`) could also be desirable on Windows, as it will automatically close the cmd window for you, regardless of success. However, you might not be able to read the error logs. The opposite of this flag is `-w` (or `--wait-exit`). On Windows, you could create a shortcut with these arguments.

### Other flags

For the rest of the flags (which are mainly there for debugging purposes), see the help (`-h` or `--help`) below:

```
Usage: phasmo_global_vc_fixer.exe [OPTIONS]...
  -h, --help           print this message and exit
  -v, --verbose        print extended debug messages
  -l, --loop           run in a loop (default)
  -s, --singleshot     don't run in a loop, quit after one fix attempt
  -w, --wait-exit      wait for user input before exiting (default on Windows)
  -q, --quick-exit     don't wait for user input before exiting
  --dont-load-cache    bypass the cache and resolve the offsets directly from the game's memory
  --dont-save-cache    don't save the offsets to cache
  --force [1/0]        force the isGhostSpawned flag to either true or false (for demonstration purposes)
```

By the way, if you are one of the lucky few who have never experienced this bug, you can force it to happen by using the `--force 0` option after you've started an investigation (`phasmo_global_vc_fixer.exe -s --force 0`). This will break the walkie-talkies of the remote players **on your end**.

## The underlying cause

**Note:** Most of this section is based on research I did back in September 2023, so Phasmo's actual code might not look like this today / variables might have been renamed since then.

In Phasmo, every `Player` object has a `PlayerAudio` object, and every `PlayerAudio` object has a `WalkieTalkie` object responsible for managing the global voice chat effect for the given player.

The walkie-talkies will only ever *apply* the spatial (local) and non-spatial (global) voice effects to their audio outputs, if the ghost has already spawned. By the way, the walkie-talkie activation itself and the subsequent hissing sound effect do not directly depend on this and are controlled by different logic. The exact details of this are not important, however, just note that upon using the global chat, the `WalkieTalkie` object's `.isOn` flag will be set to true.

Each `WalkieTalkie` object also has an `.isGhostSpawned` flag responsible for keeping track of whether the ghost has spawned. The `WalkieTalkie` objects subscribe to the `GameController.OnGhostSpawned` event in their `.Start()` method to set this flag. This event - as the name implies - is fired whenever the ghost spawns (technically for remote players it's fired by the `GhostInfo.SyncValuesNetworked()` RPC, which synchronizes the ghost info from the host).

However, *I'm suspecting* that there is a race condition here: I'm not a Unity expert, but from what I could gather, Unity does not guarantee that `.Start()` is invoked upon prefab instantiation (i.e. when the remote player objects spawn upon loading the level), but rather before the first frame update (source: https://docs.unity3d.com/Manual/ExecutionOrder.html). So if a player freezes for a long time while loading the map, then the `GhostInfo.SyncValuesNetworked()` RPC might arrive before the `WalkieTalkie.Start()` method is invoked for the remote players. Therefore, their walkie-talkie objects will never receive the `GameController.OnGhostSpawned` event, resulting in the `.isGhostSpawned` flag never being set. Hence the bug is more likely to happen for players with lower-end systems or if the game is installed on a slower storage medium (like a HDD instead of an SSD).

Below you may see the relevant parts of the decompiled (and deobfuscated) `WalkieTalkie` class. As you can see, the race condition can effectively make `Update()` useless:

```cs
public class WalkieTalkie : MonoBehaviour
{
    // ...
    private void Start()
    {
        // ...
        GameController.instance.OnGhostSpawned.AddListener(() => {isGhostSpawned = true;});
        // ...
    }

    private void Update()
    {
        if ( this.isGhostSpawned )
        {
            if ( this.isOn )
            {
                // Tries to apply the "spatialness" (so everyone on the map is able to hear it) and a static voice distortion effect.
                // It will fall back to CheckNormalVoice() if there is a ghost hunting nearby.
                this.CheckRadioVoice();
            }
            else
            {
                // Removes the "spatialness" (so only closeby players will be able to hear it).
                // It also manages the static noise effect that players hear if someone tries talking while there is a hunting ghost nearby.
                this.CheckNormalVoice();
            }
        }
    }
    // ...
}
```

### How the fix works

The local player (and thus its walkie-talkie) does not exhibit this bug (presumably because it's already on the level way before the remote players or the ghost spawns). Therefore, the fix synchronizes the `.isGhostSpawned` fields of the remote walkie-talkies with the `.isGhostSpawned` value of the local walkie-talkie. The pseudocode looks something like this:
```cs
bool localIsGhostSpawned = Network._instance.localPlayer.playerAudio.walkieTalkie.isGhostSpawned;
foreach (Network.PlayerSpot playerSpot in Network._instance.playersData)
{
    playerSpot.player.playerAudio.walkieTalkie.isGhostSpawned = localIsGhostSpawned;
}
```

Where the relevant classes:
```cs
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
```

Originally, this program started out as a simple, ~100 line proof-of-concept Python script (because I hoped that the devs would be able to fix the bug after my report), but then I ended up completely rewriting it, mostly for robustness. It automatically resolves the relevant offsets by parsing the il2cpp structs, so it should be able to survive game updates that don't drastically change the previously mentioned parts and fields.

#### The structure of the code
- `rpm.h`, `rpm.cpp`: A remote process memory manipulation helper for Windows processes that also works for Windows applications run under Wine.
- `mmap_view.h`, `mmap_view.cpp`: A helper class that can map files into memory.
- `il2cpp_rpm.h`, `il2cpp_rpm.cpp`, `il2cpp_structs.h`: Stuff for remotely reading the memory of il2cpp Unity games.
- `phasmem.h`, `phasmem.cpp`, `main.cpp` : These contain the main fix.

## Building

All you need is a C++20 ready compiler and CMake. It's a dead simple project, there aren't any external dependencies, and there are no additional parameters to configure.

### Linux

For example, while inside the cloned repository:

```sh
mkdir build && cd build
cmake .. && make -j
```

The project should build just fine with both clang and gcc. Additionally, you could also cross-compile it for Windows by using either clang-cl or [MSVC wine](https://github.com/mstorsjo/msvc-wine).

### Windows

See: https://learn.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio
