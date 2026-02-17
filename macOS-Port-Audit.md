# macOS Port Audit — S.L.O.T. Machine

Findings organized by priority. No source code has been modified.

---

## PRIORITY 1 — Blocks Compilation (Must Fix Before First Build)

### 1.1 No Xcode / macOS Export Target in Projucer

**File:** `NewProject.jucer`

The `.jucer` file contains a single `<VS2022>` export target. There is no macOS/Xcode target at all. Running Projucer and exporting will only produce a Visual Studio 2022 solution — no Xcode project exists.

**What needs to be added:**
- Open `NewProject.jucer` in Projucer on macOS.
- Add an **Xcode (macOS)** exporter under **Export Formats**.
- Set the target folder to `Builds/MacOSX`.
- Add configurations: Debug and Release.
- Set module paths to point to your local JUCE installation (currently hardcoded as `../../../JUCE/modules` — this relative path may or may not be correct on your Mac).
- Enable the **Standalone Plugin** and **VST3** targets at minimum.
- For the Standalone target, enable **AU** if you want Logic Pro support.
- Set the macOS deployment target (recommend 10.15 Catalina minimum for modern JUCE).
- Set the bundle identifier (e.g. `com.lonepearlogic.slotmachine`).
- Add code signing identity once you have a Developer ID.

**Recommendation on module paths:** The current path `../../../JUCE/modules` is relative to the `Builds/VisualStudio2022` folder, meaning it assumes JUCE sits two directories above the project root. Verify this matches your Mac file layout or set an absolute path.

---

### 1.2 Windows Resource File

**Files:** `AppIcon.rc`, `Builds/VisualStudio2022/AppIcon.rc`, `Builds/VisualStudio2022/resources.rc`

These `.rc` files are Windows resource scripts. They will not be referenced by the Xcode target, so they do not block macOS compilation directly. However, the macOS build will need:
- A proper `.icns` file (macOS icon format).
- The `.icns` file referenced in the Xcode exporter's **Custom Plist** or via Projucer's icon field.

`SlotMachine.ico` exists but is Windows-only. An `.icns` will need to be generated from the existing source artwork.

---

## PRIORITY 2 — Blocks Functionality (Compiles, but Core Features Broken)

### 2.1 License Cache — Registry Completely Non-Functional on macOS

**Files:** `Source/LicenseRegistry.h`, `Source/LicenseRegistry.cpp`

Every function in `LicenseRegistry.cpp` is wrapped in `#if JUCE_WINDOWS` with `return false` as the macOS fallback:

```
LemonSqueezyCache::saveLicenseCache()  → returns false
LemonSqueezyCache::loadLicenseCache()  → returns false
LemonSqueezyCache::hasCachedLicense()  → returns false
LemonSqueezyCache::clearLicenseCache() → returns false
```

The old V1 functions `saveLicenseToRegistry`, `loadLicenseFromRegistry`, `clearLicenseFromRegistry` are also Windows-only stubs.

**Impact:** On macOS, every call to check or save the license returns false. The application will always appear unlicensed. Any offline validation after initial activation is impossible.

**What needs to be written:**
A macOS (and cross-platform fallback) implementation for the license cache. Viable approaches in order of preference:
- **macOS Keychain** via `Security.framework` (most secure; prevents easy file tampering).
- **Encrypted file in `~/Library/Application Support/Lone Pear Logic/SlotMachine/`** using AES from the existing `crypto_small.h`.
- **JUCE `PropertiesFile`** as a simple fallback (least secure, easiest to implement quickly for testing).

The macOS Keychain approach requires linking `Security.framework` in the Xcode exporter and using `SecKeychainItem*` or the modern `Security/SecItem.h` API.

---

### 2.2 Instance Identifier — No Persistence on macOS

**File:** `Source/InstanceIdentifier.cpp`

The `#else` branch for non-Windows generates a fresh `juce::Uuid` **on every call** — it is never stored:

```cpp
// For non-Windows platforms (future support)
juce::Uuid uuid;
return uuid.toDashedString();  // new UUID every time
```

**Impact:** Every launch of the macOS app creates a brand-new machine ID. This prevents:
- License activation from persisting across relaunches.
- LemonSqueezy from recognizing the same machine instance.
- The deactivation flow from working correctly.

**What needs to be written:**
A macOS implementation that generates a stable machine identifier. Options:
- Read `IOPlatformUUID` from IOKit (the macOS equivalent of the Windows `MachineGuid` registry key).
- Store a generated UUID persistently in macOS Keychain (mirrors the Windows registry approach).
- Store in `~/Library/Application Support/Lone Pear Logic/SlotMachine/` as a hidden file.

The IOKit approach is the closest equivalent to the Windows `SOFTWARE\Microsoft\Cryptography\MachineGuid` lookup already implemented for Windows.

---

### 2.3 Updater System — Entirely Windows-Specific, Must Be Completely Rewritten

**File:** `Source/UpdateChecker.cpp`, `Source/UpdateChecker.h`

The updater system has multiple Windows-specific assumptions baked into the logic (not just guarded by `#if JUCE_WINDOWS`):

| Issue | Location | Detail |
|---|---|---|
| Looks for `SlotMachineUpdater.exe` | `launchUpdaterAndTerminate()` line 605 | Hardcoded `.exe` extension |
| Copies updater to `%TEMP%\SlotMachineUpdater\` | `launchUpdaterAndTerminate()` | Works on macOS technically via JUCE `tempDirectory`, but the binary won't exist |
| Parses `Update.txt` for `.exe` filenames | `parseUpdatesFile()` line 425 | Only matches `SlotMachineSetup-*.exe` entries |
| Version extracted between `-` and `.exe` | `VersionInfo::fromFilename()` line 66 | Hardcoded `.exe` assumption |
| DOWNLOAD_BASE_URL points to `.exe` files | `UpdateChecker.h` line 142 | Entire download pipeline assumes Windows installer |
| `startAsProcess()` on the updater `.exe` | `launchUpdaterAndTerminate()` | `.exe` won't run on macOS |

The version comparison logic, deferral system, and dialog UI are all **cross-platform** and reusable. Only the download/install mechanics need replacing.

**What needs to be rewritten:**
1. `Update.txt` format needs macOS entries (e.g. `SlotMachineSetup-01.01.00.dmg` or `.pkg`).
2. `parseUpdatesFile()` needs to detect platform and parse the correct filename.
3. `VersionInfo::fromFilename()` needs to handle `.dmg`/`.pkg` extensions, not just `.exe`.
4. `launchUpdaterAndTerminate()` needs a macOS path:
   - Download the `.dmg` or `.pkg` to `~/Downloads` using `juce::URL::downloadToFile()`.
   - Open the `.dmg` with `juce::File::startAsProcess()` (opens in Finder).
   - Or open the LonePear website download page directly as a simpler first-pass.
5. A separate `SlotMachineUpdater` helper app is **not required** on macOS — installers handle their own flow.

**Simplest first-pass macOS approach:** When an update is available, open `https://lonepearlogic.com/downloads` in the default browser instead of attempting to auto-download. This avoids the entire updater binary problem.

---

### 2.4 Windows Power Monitor — No macOS Sleep/Wake Handling

**Files:** `Source/WindowsPowerMonitor.h`, `Source/WindowsPowerMonitor.cpp`, `Source/StandaloneInit.cpp`

The power monitor class is correctly guarded with `#if JUCE_WINDOWS && JUCE_STANDALONE_APPLICATION` — it will not be compiled on macOS and will not block the build.

However, the problem it solves (audio device disconnecting after system sleep) also **exists on macOS**. JUCE provides `juce::AudioDeviceManager` which normally handles device reconnection, but custom handling may still be desirable.

**What this means for macOS:**
- The macOS standalone build compiles without the power monitor.
- Audio device reconnection after sleep is left entirely to JUCE's default behavior.
- If sleep/wake issues arise on macOS, the solution is a macOS-specific implementation using `IOKit` power notifications (`IORegisterForSystemPower`) or `NSWorkspace` notifications, wrapped in a new `#if JUCE_MAC && JUCE_STANDALONE_APPLICATION` block.

**Recommendation:** Leave as-is for the initial port. Add macOS sleep/wake handling only if users report audio device issues after sleep.

---

### 2.5 Tutorial Video Path — Will Fail in macOS App Bundle

**File:** `Source/PluginEditor.cpp` (line 7569–7590)

```cpp
const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
auto tutorialFile = executable.getParentDirectory().getChildFile("tutorialslotmachine.mp4");
```

On Windows standalone, the `.exe` and `.mp4` sit in the same directory — this works. On macOS, `currentExecutableFile` resolves to the binary inside the app bundle at `SlotMachine.app/Contents/MacOS/SlotMachine`. Calling `getParentDirectory()` gives `Contents/MacOS/`, and the `.mp4` will not be there.

**What needs to change:**
```cpp
// On macOS, app bundle resources are in Contents/Resources/
juce::File tutorialFile;
#if JUCE_MAC
    tutorialFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                       .getParentDirectory()      // MacOS/
                       .getParentDirectory()      // Contents/
                       .getChildFile("Resources/tutorialslotmachine.mp4");
#else
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    tutorialFile = executable.getParentDirectory().getChildFile("tutorialslotmachine.mp4");
#endif
```

The `.mp4` also needs to be added to the Xcode exporter's **Custom Resources** list in Projucer so it gets copied into `Contents/Resources/` during the build.

---

### 2.6 Version.txt Path — May Fail in App Bundle

**File:** `Source/UpdateChecker.cpp` (line 151–153)

```cpp
juce::File appFile = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
juce::File appDir = appFile.getParentDirectory();
juce::File versionFile = appDir.getChildFile("version.txt");
```

Same bundle structure issue as the tutorial video. On macOS, `version.txt` would need to be in `Contents/Resources/` and the path logic updated accordingly. Alternatively, the installed version could be read from the bundle's `Info.plist` (`CFBundleShortVersionString`) which is a more idiomatic macOS approach.

---

### 2.7 PluginEditor.cpp — Registry Calls Inside `#if JUCE_WINDOWS`

**File:** `Source/PluginEditor.cpp` (lines 6685–6694, 6901–6910)

```cpp
#if JUCE_WINDOWS
    if (!clearLicenseFromRegistry())
        ...
    clearLicenseFromRegistry();
#endif
```

These are already properly guarded. On macOS, the deactivation path will compile and run but `clearLicenseFromRegistry()` will be a no-op returning `false`. Once the macOS license persistence layer is implemented (see 2.1), these guards need to be extended to call the macOS equivalent.

---

## PRIORITY 3 — Installer & Distribution (Not Code, but Required for Shipping)

### 3.1 Inno Setup Installer Scripts — Windows Only

**Files:**
- `SLOT Machine Installer-01.00.00.iss`
- `SLOT Machine Installer-01.00.27.iss`
- `SLOT Machine Installer-01.00.28.iss`
- `SLOT Machine Installer-01.01.00.iss`

All installer scripts use Inno Setup, a Windows-only tool. They contain:
- Hard-coded Windows paths with backslashes (`C:\Users\Stever Leach\source\repos\...`).
- Windows-specific registry writes (`Root: HKLM; Subkey: "Software\Lone Pear Logic\SlotMachine"`).
- VST3 installation to `{cf64}\VST3` (Windows Common Files).
- Standalone `.exe` and `SlotMachineUpdater.exe` deployment.

**What is needed for macOS distribution:**
- A macOS **`.pkg` installer** (created with `pkgbuild` + `productbuild`, or tools like **Packages.app** or **WhiteBox Packages**).
- VST3 installed to `/Library/Audio/Plug-Ins/VST3/` (system-wide) or `~/Library/Audio/Plug-Ins/VST3/` (user).
- AU component installed to `/Library/Audio/Plug-Ins/Components/` or `~/Library/Audio/Plug-Ins/Components/`.
- Standalone app installed to `/Applications/` or `~/Applications/`.
- Skins folder deployed to `~/Documents/SlotMachine/Skins/` (this already matches what the code expects).
- Code signing with a Developer ID Installer certificate.
- Notarization via `notarytool` (required for Gatekeeper bypass on modern macOS).

---

### 3.2 AppIcon.rc — Windows Resource Script

**File:** `AppIcon.rc`

References `SlotMachine.ico`. This file is not compiled on macOS and has no impact on compilation, but a macOS `.icns` icon file is needed and must be added to Projucer's macOS exporter settings.

---

## PRIORITY 4 — Cosmetic / File Path Style (Minor)

### 4.1 Backslash in Filename Parsing

**File:** `Source/PluginEditor.cpp` (line 2308)

```cpp
const int slashIndex = juce::jmax(fileName.lastIndexOfChar('/'),
                                   fileName.lastIndexOfChar('\\'));
```

This correctly handles both `/` (macOS/Linux) and `\\` (Windows) separators. **No change needed** — this is already cross-platform.

### 4.2 `juce::File::userApplicationDataDirectory`

**File:** `Source/PluginEditor.cpp` (line 2447), `Source/UpdateChecker.cpp` (line 84)

```cpp
juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
    .getChildFile("Lone Pear Logic")
    .getChildFile("SlotMachine");
```

On Windows this resolves to `C:\Users\<user>\AppData\Roaming\Lone Pear Logic\SlotMachine\`.
On macOS this resolves to `~/Library/Application Support/Lone Pear Logic/SlotMachine/`.

**JUCE handles this correctly and cross-platform.** No change needed. `options.xml` will be stored in the right place on both platforms.

### 4.3 `juce::File::userDocumentsDirectory` for Skins

**File:** `Source/AppLookAndFeel.h` (line 601–606)

```cpp
return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
           .getChildFile("SlotMachine")
           .getChildFile("Skins");
```

On macOS this resolves to `~/Documents/SlotMachine/Skins/`. The installer will need to copy the skins here, which is already done in the `.iss` script via `{userdocs}\SlotMachine\Skins`. The macOS `.pkg` installer must do the same. **Code is fine; only installer needs equivalent.**

---

## PRIORITY 5 — Font Dependencies

### 5.1 Custom TTF Fonts — Bundled with Skins

**Files found in skins:**
- `LED.ttf` — Used in Studio Steel (1/2/3) and Midnight Forge skins
- `Montserrat-SemiBold.ttf` — Used in Oak skin

Both fonts are loaded at runtime from the skin folder using:
```cpp
juce::Typeface::createSystemTypefaceFor(fontData.getData(), fontData.getSize())
```

This is entirely cross-platform. JUCE loads the font data from the file into memory and creates a typeface without any OS font registry involvement. **These fonts will work on macOS as-is**, provided the skin files are deployed to `~/Documents/SlotMachine/Skins/`.

### 5.2 No Hardcoded System Font Names

A scan of the entire codebase found no hardcoded references to Windows system fonts (e.g. `Segoe UI`, `Tahoma`, `Arial`). JUCE's default `LookAndFeel_V4` uses its own built-in fonts. The skin-defined fonts are loaded from TTF files. **No font compatibility issues.**

---

## SUMMARY TABLE

| # | File(s) | Issue | Priority | Effort |
|---|---------|-------|----------|--------|
| 1 | `NewProject.jucer` | No Xcode export target — cannot build at all | **P1 — Compilation** | Low (Projucer UI) |
| 2 | `AppIcon.rc` / `.ico` | Windows resource files; need `.icns` for macOS | P1 (cosmetic) | Low |
| 3 | `LicenseRegistry.cpp` | All functions return `false` on macOS; no persistence | **P2 — Functionality** | High |
| 4 | `InstanceIdentifier.cpp` | Generates new UUID every launch; no stable machine ID | **P2 — Functionality** | Medium |
| 5 | `UpdateChecker.cpp` | Entire updater assumes `.exe`; must be rewritten for macOS | **P2 — Functionality** | High |
| 6 | `WindowsPowerMonitor.*` | Windows-only sleep/wake; correctly guarded; macOS gets no handler | P2 (optional) | Medium (if needed) |
| 7 | `PluginEditor.cpp:7572` | Tutorial video path wrong inside macOS app bundle | **P2 — Functionality** | Low |
| 8 | `UpdateChecker.cpp:151` | `version.txt` path wrong inside macOS app bundle | **P2 — Functionality** | Low |
| 9 | `*.iss` scripts | Windows Inno Setup; need macOS `.pkg` equivalent | P3 — Distribution | High |
| 10 | `LED.ttf`, `Montserrat-SemiBold.ttf` | Bundled TTF; loaded via JUCE memory API; fully cross-platform | None | None |

---

## RECOMMENDED IMPLEMENTATION ORDER

1. **Add Xcode export target in Projucer** — required before any macOS build attempt.
2. **Fix app bundle resource paths** for `version.txt` and `tutorialslotmachine.mp4` — low effort, unblocks basic standalone testing.
3. **Implement macOS license cache** (Keychain or encrypted file) to replace registry — needed for any licensed user testing.
4. **Implement stable macOS machine identifier** (IOKit UUID or Keychain-stored UUID) — needed alongside the license cache.
5. **Rewrite updater for macOS** — can ship initially as "open download page in browser" and add auto-download later.
6. **Create macOS `.pkg` installer** — needed for shipping; can use a simple drag-to-Applications approach for early beta.
7. **Generate `.icns` icon** from existing artwork — polish step.
8. **Add macOS sleep/wake audio handling** — only if user reports indicate it is needed.

---

*Analysis produced by Claude. No source code was modified.*
