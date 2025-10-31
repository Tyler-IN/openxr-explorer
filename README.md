# OpenXR Explorer
![Screenshot of OpenXR Explorer](docs/OpenXRExplorerWindow.png)
OpenXR Explorer is a handy debug tool for OpenXR developers. It allows for easy switching between OpenXR runtimes, shows lists of the runtime's supported extensions, and allows for inspection of common properties and enumerations, with direct links to relevant parts of the OpenXR specification!

## Download
Get the latest pre-compiled Windows and Linux binaries over in [the releases tab](https://github.com/maluoi/openxr-explorer/releases)!

## Features
### Runtime Switching
![Runtime switching](docs/OpenXRExplorerSwitcher.gif)

If you hop between runtimes often, whether it be for testing, experimenting, or whatever else, you know it can be a bit painful! OpenXR Explorer adds a simple dropdown to manage this, with a configurable list of runtimes for those with in-development runtimes, or non-standard install directories. Permission elevation is requested via a separate switching application, so OpenXR Explorer itself doesn't need admin!

And speaking of a separate application, that application is `xrsetruntime`, and is easily accessible via command line for those with a CLI workflow! Try `xrsetruntime -WMR` from an elevated console.

### Runtime Information
![Runtime information and docs](docs/OpenXRExplorerExtensions.gif)

It can be handy to know what to expect when requesting data from OpenXR! This tool shows all the common lists and enumerations, and provides quick links to the relevant section of the OpenXR specification for additional details. This is a great way to quickly see differences between runtimes, or plan out your own OpenXR applications!

### Command Line Interface
![Command line example](docs/OpenXRExplorerCLI.gif)

Just about everything you see in the GUI is also available in text format when used from the command line! If you provide the openxr-explorer application with function or type names as arguments, it'll just dump the results as text to the console instead of launching the GUI. Who needs this? I don't know! I sure didn't, but I hope someone else does :)

CLI flags:
- `-help` | `-h`: Show help for CLI usage.
- `-session`: Create an XrSession in CLI mode. Needed for queries that require a Session (e.g., view config views, reference spaces on some runtimes).
- `-gpuLogLevel <level>` | `-gpuLogLevel=<level>`: Control GPU/renderer (sk_gpu) log verbosity printed by CLI.
  - Levels: info, warn (default), error.
- `-loaderDebug <level>` | `-loaderDebug=<level>`: Control OpenXR Loader verbosity via `XR_LOADER_DEBUG`.
  - Default is error if not already set. Levels: error, warn, info, verbose, trace.
- `-loaderLogFile <path>` | `-loaderLogFile=<path>`: Redirect OpenXR Loader logs to a file via `XR_LOADER_LOG_FILE`.
- And many more! See `-help` for details.

Examples:
```
openxr-explorer -session -xrEnumerateReferenceSpaces
openxr-explorer -session -gpuLogLevel info -xrEnumerateViewConfigurationViews
openxr-explorer -session -loaderDebug info -loaderLogFile loader.log -xrEnumerateInstanceExtensionProperties
```

### Building
If you just want to use it, see the [Releases](https://github.com/maluoi/openxr-explorer/releases) tab! If you want to build it or modify it, then OpenXR Explorer uses cmake.

#### Windows
From the root directory:
```
mkdir build
cd build
cmake ..
cmake --build . --config Release --parallel 8
cd Release
openxr-explorer.exe
```
#### Linux
Pre-requisites (partial)
```
sudo apt-get install libxcb-keysyms1-dev libxcb1-dev libxcb-xfixes0-dev libxcb-cursor-dev libxcb-xkb-dev libxcb-glx0-dev libxcb-randr0-dev libx11-xcb-dev libglew-dev
```

From the root directory:
```
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel 8

# Optionally, to install the executables:
sudo cmake --install . --prefix /usr/local

./openxr-explorer
```

### Contributing
OpenXR is a living API, and there's new extensions coming out all the time! If you think there's something OpenXR Explorer should be displaying, then heck yeah I'll take a pull request! The application is architected to easily allow for additional information. All you need to do is add a new `display_table_t` to the `xr_tables` list, and you're good to go! See `openxr_info.cpp` for reference.

Found a new runtime manifest you think should be included by default? Add it to [xr_runtime_default.h](https://github.com/maluoi/openxr-explorer/blob/main/src/common/xr_runtime_default.h), or raise it as an Issue :)

### Relevant Stuff
If you're learning OpenXR, check out this [introductory tutorial](https://playdeck.net/blog/introduction-to-openxr) I also wrote!

If you're just interested in building OpenXR based applications, consider [StereoKit](https://stereokit.net/), a cross-platform Mixed Reality Engine for C# and C++ that I also wrote! It just might save you some time :)