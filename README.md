# Audio Capture - C++ Version

Real-time Windows system audio recorder with live waveform visualization using WASAPI.

## Features

- **Real-time Audio Capture** - Captures system audio using WASAPI loopback
- **Live Waveform Display** - Shows real-time waveform visualization
- **WAV File Export** - Records audio directly to WAV format
- **Win32 GUI** - Native Windows application interface
- **Multi-threading** - Efficient background audio processing

## System Requirements

- Windows Vista or later (WASAPI support)
- Visual Studio 2019 or later
- Windows 10 SDK

## Build Instructions

### Using Visual Studio

1. Open `AudioCapture.vcxproj` in Visual Studio
2. Set your desired configuration (Release recommended for deployment)
3. Build the project (Ctrl + Shift + B)
4. The executable will be in the `x64\Release` or `Win32\Release` folder

### Using Command Line (MSBuild)

```cmd
msbuild AudioCapture.vcxproj /p:Configuration=Release /p:Platform=x64
```

### Using CMake (Alternative)

```cmd
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## Running the Application

1. Run `AudioCaptureCpp.exe` 
2. Click "Start Recording" to begin capturing system audio
3. The waveform will display in real-time
4. Click "Stop Recording" to save the WAV file
5. Recordings are saved as `recording_1.wav`, `recording_2.wav`, etc.

## How It Works

### WASAPI Loopback Capture

The application uses the Windows Audio Session API (WASAPI) to capture system audio:

1. **Device Enumeration** - Finds the default audio output device
2. **Loopback Activation** - Activates loopback mode to capture system audio
3. **Buffer Processing** - Continuously reads audio frames from the capture buffer
4. **Real-time Visualization** - Updates waveform display every 100ms

### Audio Processing

- Sample Rate: 44.1 kHz (or device default)
- Bit Depth: 16-bit PCM
- Channels: 2 (Stereo)
- File Format: WAV

## Architecture

### Files

- `audio_capture.h` - Audio capture interface and WASAPI wrapper
- `audio_capture.cpp` - WASAPI implementation with thread-safe buffering
- `main.cpp` - Win32 GUI and application logic

### Key Classes

#### AudioCapture

Main class encapsulating WASAPI functionality:

```cpp
class AudioCapture {
    bool Initialize();           // Initialize WASAPI interfaces
    bool StartRecording();       // Start audio capture
    bool StopRecording();        // Stop and save recording
    std::vector<float> GetWaveformBuffer();  // Get visualization data
};
```

## Notes

- Requires elevated privileges (Admin mode) on some Windows versions
- System audio must be enabled for loopback capture to work
- Some security software may block loopback capture

## Troubleshooting

### No Audio Captured
- Ensure system audio is enabled
- Check that your audio device supports loopback
- Try running as Administrator

### Slow Performance
- Close unnecessary applications
- Disable intensive visual effects
- Check CPU and memory usage

### Build Errors
- Ensure Windows 10 SDK is installed
- Update Visual Studio to the latest version
- Check that all required libraries are linked

## Future Enhancements

- [ ] Real-time frequency analysis (FFT)
- [ ] Multiple device support
- [ ] Audio effects and filtering
- [ ] MP3/FLAC export
- [ ] Audio playback from recordings
- [ ] Advanced visualization options

## License

This is a demonstration application for educational purposes.

## References

- [WASAPI Documentation](https://docs.microsoft.com/en-us/windows/win32/coreaudio/wasapi)
- [Win32 API Documentation](https://docs.microsoft.com/en-us/windows/win32/api/)
- [Audio Loopback Documentation](https://docs.microsoft.com/en-us/windows/win32/coreaudio/loopback-recording)
