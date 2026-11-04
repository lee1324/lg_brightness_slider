# LG-Ultrafine-Brightness

A tool to adjust brightness of LG Ultrafine 4k/5K on Windows without the help of Bootcamp.

The program sends set feature report to the device to adjust the brightness.

This project is derived from [https://github.com/unknownzerx/lguf-brightness](https://github.com/unknownzerx/lguf-brightness).

Thanks @unknownzerx.

### Linux
Please go to this repo [https://github.com/velum/lguf-brightness](https://github.com/velum/lguf-brightness).

### Usage

1. Connect your LG Ultrafine monitor.
2. Run the app (x64).
3. Drag the **slider** to adjust brightness.
4. **Min** and **Max** jump to the ends of the range.
5. Close the window or press `Esc` to quit.

### Build

1. Open the `sln` file in Visual Studio.
2. Select `x64` configuration.
3. Build!

Note that `hidapi.dll` is included in the project. If you prefer, build a new one (x64) and replace it.
