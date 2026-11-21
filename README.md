# AudioPass

AudioPass sends an input device through a chain of VST2 and VST3 plugins and writes the result to one output: a playback device or a WAV file. Other applications hear that output only through the device's loopback entry, for example "Loop-back 1/2 (loopback)" on an Audient iD4 or "CABLE Output" when using VB-Cable.

<p align="center" style="margin: 40px 0;">
    <img src="docs/screenshot.png" alt="Audiopass 0.90"><br/>
    Plugins in the screenshot are examples only and are not included with AudioPass.
</p>  

## Using AudioPass

Under **INPUT**, choose the microphone. **Stereo** keeps the two channels separate. **Mono** folds left and right into one signal and places it on both channels.

**OUTPUT** is the destination. Virtual outputs are listed first. **File** writes a WAV file, and the file name follows the arrow. Speakers can be selected, but they easily cause feedback. A yellow note appears when the output is not a virtual cable.

**Start** opens the route. **Stop** closes it. While the route is running, the bars show the peak level. Below them, a VU meter runs from −40 dB to 0 dB. **min** and **max** are the quietest and loudest values since the last start.

**HISTORY** scrolls the last 50 seconds of input (blue) and output (orange). The thin line is peak, the thick line is RMS. **-60**, **-36**, and **-24** set the bottom of the scale. The top stays at +6 dB. A red mark means the peak reached 0 dBFS. **Both**, **Peak**, and **RMS** choose which lines are drawn. The scale and the line choice are remembered. **Freeze** holds the trace and **Reset** clears it. Starting again also clears it.

**Record** writes the history, one sample about every 50 ms: input peak, input RMS, output peak, output RMS. **Stop** closes that file. The next recording starts a new file named `levels-YYYYMMDD-HHMMSS.csv`. With **Save level history as binary** on, the same take is a compact `.apl` file instead. **Load** opens a CSV or `.apl` file in its own window.

**GAIN** lowers or raises the output from −60 dB to +12 dB. **−** and **+** step by 0.5 dB. Double-click returns it to 0 dB.

### NVIDIA

**Noise removal** and **Room echo removal** sit in their own section before the plugin chain. They require the [NVIDIA Broadcast SDK](https://www.nvidia.com/en-us/geforce/broadcasting/broadcast-sdk/resources/) and run on a mono mix before every VST plugin. **Strength** runs from 0% to 100%. 0% leaves the signal unchanged. Without that SDK the switches stay off and the rest of AudioPass is unchanged.

### Plugin chain

**Add** opens the plugin list. AudioPass scans the usual VST folders. More folders can be added there. A plugin file can also be dropped onto the window.

Order is the signal path from top to bottom. Drag a row to reorder it, or use the arrows on the row and the Up and Down keys while the list has focus. Delete or Backspace removes the selected entry. A click opens the editor when the plugin has one.

The buffer sizes **128**, **256**, and **512** samples sit at the bottom.

### Window and startup

**On top** keeps the window above the others.

**Settings** stores the switches below, and the folder used by **Record**:

- **Minimize to the system tray** hides the window in the notification area when it is minimized.
- **Minimize instead of quitting** turns the close button into minimize. With the tray option on, the window goes there.
- **Start with Windows** launches AudioPass when you sign in.
- **Start audio when AudioPass opens** presses Start after the chain has loaded.
- **Level history folder** is where **Record** writes `levels-YYYYMMDD-HHMMSS.csv` or `.apl`.
- **Save level history as binary** stores the take as `.apl`. Off keeps the CSV file.

While either of the first two options is on, the tray icon stays visible. A click brings the window back. Quit from the menu with **Exit**.

Size, position, and the other settings are stored in `%APPDATA%\AudioPass`. **About** shows the version and the VST notice.

## Building

AudioPass builds on Windows with Visual Studio 2026 and CMake. The first configure downloads wxWidgets 3.2.11 and the VST 3 SDK 3.8.1 into `third_party` when those directories are missing. Git is required for that download. The libraries are not part of the repository.

```bat
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

The executable is `build\bin\Release\AudioPass.exe`. The same steps are in `build-release.ps1` and `build-debug.ps1`.

## License

The AudioPass source is released under the MIT License. Copyright (c) 2026 Citiga. The full text is [LICENSE.md](LICENSE.md).

AudioPass uses wxWidgets 3.2.11 under the wxWindows Library Licence, Version 3.1. zlib, libpng, and PCRE2 are used inside wxWidgets. Their full license texts are in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

This software is based in part on the work of the Independent JPEG Group.

libtiff, used by wxWidgets:

```
Copyright (c) 1988-1997 Sam Leffler
Copyright (c) 1991-1997 Silicon Graphics, Inc.

Permission to use, copy, modify, distribute, and sell this software and
its documentation for any purpose is hereby granted without fee, provided
that (i) the above copyright notices and this permission notice appear in
all copies of the software and related documentation, and (ii) the names of
Sam Leffler and Silicon Graphics may not be used in any advertising or
publicity relating to the software without the specific, prior written
permission of Sam Leffler and Silicon Graphics.

THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND,
EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.

IN NO EVENT SHALL SAM LEFFLER OR SILICON GRAPHICS BE LIABLE FOR
ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL DAMAGES OF ANY KIND,
OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
WHETHER OR NOT ADVISED OF THE POSSIBILITY OF DAMAGE, AND ON ANY THEORY OF
LIABILITY, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
OF THIS SOFTWARE.
```

Expat, used by wxWidgets:

```
Copyright (c) 1998-2000 Thai Open Source Software Center Ltd and Clark Cooper
Copyright (c) 2001-2019 Expat maintainers

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```

Parts of the VST 3 SDK 3.8.1 are compiled into AudioPass. The VST 2 host interface in this repository is an independent description of the plugin binary interface. The VST 2 SDK is not included.

```
MIT License

Copyright (c) 2026, Steinberg Media Technologies GmbH

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

VST is a trademark of Steinberg Media Technologies GmbH, registered in Europe and other countries.

Noise removal and room echo removal can load the NVIDIA Audio Effects SDK when that SDK is already installed. AudioPass does not include, link, or distribute the SDK, its models, or NVIDIA's runtime libraries. They remain under NVIDIA's own license. NVIDIA is a trademark of NVIDIA Corporation. AudioPass is not sponsored or endorsed by NVIDIA.
