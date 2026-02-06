#include "audio_capture.h"
#include <mmsystem.h>
#include <chrono>
#include <string>
#include <cmath>
#include <propkey.h>
#include <propidl.h>
#include <propsys.h>
#include <fstream>
#include <sstream>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")

const int WAVEFORM_BUFFER_SIZE = 48000; // 1 second at 48kHz (reduced for performance)
const REFERENCE_TIME REFTIMES_PER_SEC = 10000000;
const REFERENCE_TIME REFTIMES_PER_MILLISEC = 10000;

// Logging function
void LogError(const char* message) {
    try {
        std::ofstream log("error_log.txt", std::ios::app);
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        log << "[" << std::ctime(&time) << "] " << message << std::endl;
        log.close();
    } catch (...) {
        // Ignore logging errors
    }
}

void ShowError(const wchar_t* message, HRESULT hr) {
    wchar_t buffer[256];
    swprintf_s(buffer, L"%s\nHRESULT: 0x%08X", message, hr);
    MessageBoxW(nullptr, buffer, L"Audio Capture Error", MB_OK | MB_ICONERROR);
    
    // Log the error
    std::string logMsg = "ShowError called: ";
    logMsg += std::string(message, message + wcslen(message));
    logMsg += " HRESULT: 0x" + std::to_string(hr);
    LogError(logMsg.c_str());
}

AudioCapture::AudioCapture()
{
    m_waveformBuffer.resize(WAVEFORM_BUFFER_SIZE, 0.0f);
    m_waveformBufferSize = WAVEFORM_BUFFER_SIZE;
}

AudioCapture::~AudioCapture()
{
    StopRecording();
    if (m_audioFile != INVALID_HANDLE_VALUE) {
        CloseHandle(m_audioFile);
    }
}

bool AudioCapture::Initialize()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        ShowError(L"Failed to initialize COM", hr);
        return false;
    }

    return InitializeWASAPI();
}

bool AudioCapture::StartCapture()
{
    if (m_isCapturing) return true;

    HRESULT hr = m_audioClient->Start();
    if (FAILED(hr)) {
        ShowError(L"Failed to start audio capture", hr);
        return false;
    }

    m_isCapturing = true;
    m_captureThread = std::make_unique<std::thread>(&AudioCapture::CaptureThread, this);
    return true;
}

bool AudioCapture::StopCapture()
{
    if (!m_isCapturing) return true;

    try {
        m_isCapturing = false;
        m_stopCapture = true;  // Signal thread to stop
        
        // Stop audio client immediately
        if (m_audioClient) {
            m_audioClient->Stop();
            m_audioClient->Reset();
        }
        
        // Wait for capture thread with timeout (max 500ms)
        if (m_captureThread && m_captureThread->joinable()) {
            // Try to join with timeout
            auto waitStart = std::chrono::high_resolution_clock::now();
            while (m_captureThread->joinable()) {
                Sleep(10);
                auto elapsed = std::chrono::high_resolution_clock::now() - waitStart;
                if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > 500) {
                    // Timeout - detach thread
                    m_captureThread->detach();
                    break;
                }
            }
            
            // Try final join if still joinable
            if (m_captureThread->joinable()) {
                m_captureThread->join();
            }
        }
        
        m_captureThread.reset();
        m_stopCapture = false;
        return true;
    }
    catch (const std::exception& e) {
        // Log exception
        LogError("Exception in StopCapture");
        OutputDebugStringW(L"Exception in StopCapture\n");
        return false;
    }
}

bool AudioCapture::InitializeWASAPI()
{
    HRESULT hr;

    // Create device enumerator
    hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), nullptr,
        CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
        (void**)m_deviceEnumerator.GetAddressOf());
    if (FAILED(hr)) {
        ShowError(L"Failed to create device enumerator", hr);
        return false;
    }

    // Use selected device if available, otherwise get default audio endpoint (loopback for system audio capture)
    if (!m_deviceSelected) {
        // For system audio, we need to get the render device and use loopback
        hr = m_deviceEnumerator->GetDefaultAudioEndpoint(
            eRender, eConsole, m_device.GetAddressOf());
        if (FAILED(hr)) {
            ShowError(L"Failed to get default audio endpoint", hr);
            return false;
        }
        
        // Set current device info for default device
        m_currentDevice.index = -1; // Default device
        m_currentDevice.name = L"Default System Device";
        m_currentDevice.id = L"default";
        m_currentDevice.isDefault = true;
        m_deviceSelected = true; // Mark as selected to avoid re-initialization
    }
    // If m_deviceSelected is true, m_device should already be set in SelectAudioDevice()

    // Debug: Write current device info
    {
        HANDLE debugFile = CreateFileW(L"init_debug.txt", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (debugFile != INVALID_HANDLE_VALUE) {
            std::string debugText = "Initializing device: " + std::string(m_currentDevice.name.begin(), m_currentDevice.name.end()) + "\n";
            debugText += "Device type: " + std::string(m_currentDeviceType == RenderDevices ? "Render" : "Capture") + "\n";
            debugText += "Device index: " + std::to_string(m_currentDevice.index) + "\n";
            DWORD written;
            WriteFile(debugFile, debugText.c_str(), (DWORD)debugText.length(), &written, nullptr);
            CloseHandle(debugFile);
        }
    }

    // Create audio client
    hr = m_device->Activate(
        __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
        (void**)m_audioClient.GetAddressOf());
    if (FAILED(hr)) {
        ShowError(L"Failed to activate audio client", hr);
        return false;
    }

    // Get device format
    WAVEFORMATEX* pwfx = nullptr;
    hr = m_audioClient->GetMixFormat(&pwfx);
    if (FAILED(hr)) {
        ShowError(L"Failed to get mix format", hr);
        return false;
    }

    // For loopback capture, use a standard PCM format
    // Copy the basic parameters but ensure it's PCM
    m_waveFormat.wFormatTag = WAVE_FORMAT_PCM;
    m_waveFormat.nChannels = pwfx->nChannels;  // Usually 2 for stereo
    m_waveFormat.nSamplesPerSec = pwfx->nSamplesPerSec;  // Usually 44100 or 48000
    m_waveFormat.wBitsPerSample = 16;  // Use 16-bit for compatibility
    m_waveFormat.nBlockAlign = m_waveFormat.nChannels * m_waveFormat.wBitsPerSample / 8;
    m_waveFormat.nAvgBytesPerSec = m_waveFormat.nSamplesPerSec * m_waveFormat.nBlockAlign;
    m_waveFormat.cbSize = 0;

    CoTaskMemFree(pwfx);

    // Check if the format is supported
    WAVEFORMATEX* closestMatch = nullptr;
    hr = m_audioClient->IsFormatSupported(AUDCLNT_SHAREMODE_SHARED, &m_waveFormat, &closestMatch);
    if (FAILED(hr)) {
        if (hr == AUDCLNT_E_UNSUPPORTED_FORMAT && closestMatch) {
            // Use the closest supported format
            m_waveFormat = *closestMatch;
            CoTaskMemFree(closestMatch);
        } else {
            ShowError(L"Audio format not supported", hr);
            CoTaskMemFree(pwfx);
            return false;
        }
    }

    // Initialize audio client for capture
    DWORD streamFlags = 0;
    if (m_currentDeviceType == RenderDevices) {
        // For render devices, use loopback capture
        streamFlags = AUDCLNT_STREAMFLAGS_LOOPBACK;
    }
    // For capture devices, use direct capture (no flags needed)

    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        streamFlags,
        REFTIMES_PER_SEC,
        0,
        &m_waveFormat,
        nullptr);
    if (FAILED(hr)) {
        ShowError(L"Failed to initialize audio client for capture", hr);
        return false;
    }

    // Get buffer size
    hr = m_audioClient->GetBufferSize(&m_bufferFrameCount);
    if (FAILED(hr)) {
        ShowError(L"Failed to get buffer size", hr);
        return false;
    }

    // Get capture client
    hr = m_audioClient->GetService(
        __uuidof(IAudioCaptureClient),
        (void**)m_captureClient.GetAddressOf());
    if (FAILED(hr)) {
        ShowError(L"Failed to get capture client", hr);
        return false;
    }

    return true;
}

bool AudioCapture::StartRecording(const wchar_t* filename)
{
    if (m_isRecording) return false;

    // Create file
    m_audioFile = CreateFileW(
        filename,
        GENERIC_WRITE,
        0,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    
    if (m_audioFile == INVALID_HANDLE_VALUE) return false;

    // Write dummy WAV header (will update on stop)
    WriteWaveHeader();

    m_bytesWritten = 44; // WAV header size
    m_sampleCount = 0;
    m_waveformPos = 0;
    m_isRecording = true;

    // Start recording thread
    m_recordingThread = std::make_unique<std::thread>([this]() { RecordingThread(); });

    return true;
}

bool AudioCapture::StopRecording()
{
    if (!m_isRecording) return false;

    m_isRecording = false;

    // Wait for thread
    if (m_recordingThread && m_recordingThread->joinable()) {
        m_recordingThread->join();
    }

    // Update WAV header with actual data size
    UpdateWaveHeader();

    if (m_audioFile != INVALID_HANDLE_VALUE) {
        CloseHandle(m_audioFile);
        m_audioFile = INVALID_HANDLE_VALUE;
    }

    return true;
}

void AudioCapture::WriteWaveHeader()
{
    // RIFF header
    char header[44] = {};
    
    // "RIFF"
    header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
    
    // File size - 8
    *(uint32_t*)(header + 4) = 36; // Will update
    
    // "WAVE"
    header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
    
    // "fmt "
    header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
    
    // Subchunk1Size
    *(uint32_t*)(header + 16) = 16;
    
    // AudioFormat (PCM = 1)
    *(uint16_t*)(header + 20) = 1;
    
    // NumChannels
    *(uint16_t*)(header + 22) = m_waveFormat.nChannels;
    
    // SampleRate
    *(uint32_t*)(header + 24) = m_waveFormat.nSamplesPerSec;
    
    // ByteRate
    *(uint32_t*)(header + 28) = m_waveFormat.nAvgBytesPerSec;
    
    // BlockAlign
    *(uint16_t*)(header + 32) = m_waveFormat.nBlockAlign;
    
    // BitsPerSample
    *(uint16_t*)(header + 34) = m_waveFormat.wBitsPerSample;
    
    // "data"
    header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
    
    // Subchunk2Size
    *(uint32_t*)(header + 40) = 0; // Will update

    DWORD written = 0;
    WriteFile(m_audioFile, header, 44, &written, nullptr);
}

void AudioCapture::UpdateWaveHeader()
{
    // Calculate sizes
    uint32_t dataSize = m_bytesWritten - 44;
    uint32_t fileSize = m_bytesWritten - 8;

    // Seek to file size position
    SetFilePointer(m_audioFile, 4, nullptr, FILE_BEGIN);
    DWORD written = 0;
    WriteFile(m_audioFile, &fileSize, 4, &written, nullptr);

    // Seek to data size position
    SetFilePointer(m_audioFile, 40, nullptr, FILE_BEGIN);
    WriteFile(m_audioFile, &dataSize, 4, &written, nullptr);
}

void AudioCapture::RecordingThread()
{
    const DWORD flags = AUDCLNT_BUFFERFLAGS_SILENT;
    UINT32 nextPacketSize = 0;

    while (m_isRecording) {
        Sleep(10); // Small delay to avoid busy waiting

        // Get size of next capture package
        HRESULT hr = m_captureClient->GetNextPacketSize(&nextPacketSize);
        if (FAILED(hr)) continue;

        // Process all available packets
        while (nextPacketSize > 0) {
            BYTE* data = nullptr;
            UINT32 numFramesAvailable = 0;
            DWORD streamFlags = 0;

            hr = m_captureClient->GetBuffer(&data, &numFramesAvailable, &streamFlags, nullptr, nullptr);
            if (FAILED(hr)) break;

            // Convert audio data
            int32_t bytesToWrite = numFramesAvailable * m_waveFormat.nBlockAlign;

            if (streamFlags & AUDCLNT_BUFFERFLAGS_SILENT) {
                // Write silence
                std::vector<uint8_t> silence(bytesToWrite, 0);
                DWORD written = 0;
                WriteFile(m_audioFile, silence.data(), bytesToWrite, &written, nullptr);
            } else {
                // Write actual audio data
                DWORD written = 0;
                WriteFile(m_audioFile, data, bytesToWrite, &written, nullptr);

                // Update waveform buffer for visualization
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    
                    if (m_waveFormat.wBitsPerSample == 16) {
                        int16_t* pcmData = (int16_t*)data;
                        for (UINT32 i = 0; i < numFramesAvailable; i++) {
                            float sample = pcmData[i * m_waveFormat.nChannels] / 32768.0f;
                            
                            // Use circular buffer instead of shifting
                            m_waveformBuffer[m_waveformPos] = sample;
                            m_waveformPos = (m_waveformPos + 1) % m_waveformBufferSize;
                            
                            m_sampleCount++;
                        }
                    }
                }
            }

            m_bytesWritten += bytesToWrite;

            m_captureClient->ReleaseBuffer(numFramesAvailable);

            hr = m_captureClient->GetNextPacketSize(&nextPacketSize);
            if (FAILED(hr)) break;
        }
    }
}

void AudioCapture::CaptureThread()
{
    const DWORD flags = AUDCLNT_BUFFERFLAGS_SILENT;
    UINT32 nextPacketSize = 0;

    // Debug counter
    static int debugCounter = 0;
    static int packetCounter = 0;

    while (m_isCapturing && !m_stopCapture) {
        // Early exit check
        if (m_stopCapture) break;
        
        Sleep(10); // Small delay to avoid busy waiting

        // Get size of next capture package
        HRESULT hr = m_captureClient->GetNextPacketSize(&nextPacketSize);
        if (FAILED(hr)) {
            if (m_stopCapture) break;
            continue;
        }

        // Debug: Write packet info every 100 iterations
        debugCounter++;
        if (debugCounter % 100 == 0) {
            HANDLE debugFile = CreateFileW(L"capture_debug.txt", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (debugFile != INVALID_HANDLE_VALUE) {
                std::string debugText = "Packets received: " + std::to_string(packetCounter) + "\n";
                debugText += "Last packet size: " + std::to_string(nextPacketSize) + "\n";
                DWORD written;
                WriteFile(debugFile, debugText.c_str(), (DWORD)debugText.length(), &written, nullptr);
                CloseHandle(debugFile);
            }
        }

        // Process all available packets
        while (nextPacketSize > 0) {
            BYTE* data = nullptr;
            UINT32 numFramesAvailable = 0;
            DWORD streamFlags = 0;

            hr = m_captureClient->GetBuffer(&data, &numFramesAvailable, &streamFlags, nullptr, nullptr);
            if (FAILED(hr)) break;

            packetCounter++;

            if (!(streamFlags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                // Debug: Check if we have non-silent data
                static int silentCounter = 0;
                static int dataCounter = 0;
                dataCounter++;
                
                if (dataCounter % 50 == 0) {  // Every 50th data packet
                    HANDLE debugFile = CreateFileW(L"audio_data_debug.txt", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (debugFile != INVALID_HANDLE_VALUE) {
                        std::string debugText = "Data packets: " + std::to_string(dataCounter) + "\n";
                        debugText += "Silent packets: " + std::to_string(silentCounter) + "\n";
                        debugText += "Frames available: " + std::to_string(numFramesAvailable) + "\n";
                        debugText += "Channels: " + std::to_string(m_waveFormat.nChannels) + "\n";
                        debugText += "Bits per sample: " + std::to_string(m_waveFormat.wBitsPerSample) + "\n";
                        
                        // Check first few samples
                        if (m_waveFormat.wBitsPerSample == 16 && numFramesAvailable > 0) {
                            int16_t* pcmData = (int16_t*)data;
                            debugText += "First sample: " + std::to_string(pcmData[0]) + "\n";
                            if (numFramesAvailable > 1) {
                                debugText += "Second sample: " + std::to_string(pcmData[1]) + "\n";
                            }
                        }
                        
                        DWORD written;
                        WriteFile(debugFile, debugText.c_str(), (DWORD)debugText.length(), &written, nullptr);
                        CloseHandle(debugFile);
                    }
                }

                // Update waveform buffer for visualization
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    
                    if (m_waveFormat.wBitsPerSample == 16) {
                        int16_t* pcmData = (int16_t*)data;
                        for (UINT32 i = 0; i < numFramesAvailable; i++) {
                            float sample = pcmData[i * m_waveFormat.nChannels] / 32768.0f;
                            
                            // Shift waveform buffer
                            for (int j = 0; j < WAVEFORM_BUFFER_SIZE - 1; j++) {
                                m_waveformBuffer[j] = m_waveformBuffer[j + 1];
                            }
                            m_waveformBuffer[WAVEFORM_BUFFER_SIZE - 1] = sample;
                            
                            m_sampleCount++;
                        }
                    }
                }
            } else {
                static int silentCounter = 0;
                silentCounter++;
            }

            m_captureClient->ReleaseBuffer(numFramesAvailable);

            hr = m_captureClient->GetNextPacketSize(&nextPacketSize);
            if (FAILED(hr)) break;
        }
    }
}

// Removed - use GetWaveformBuffer() pointer directly with GetWaveformMutex()

float AudioCapture::GetCurrentLevel() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_waveformBuffer.empty()) return 0.0f;
    
    // Calculate RMS of last 2400 samples (50ms at 48kHz) or less if buffer is small
    const int numSamples = min(2400, (int)m_waveformBuffer.size());
    float sum = 0.0f;
    
    // Calculate from the last numSamples positions (accounting for circular buffer)
    for (int i = 0; i < numSamples; i++) {
        int idx = (m_waveformPos - numSamples + i + m_waveformBufferSize) % m_waveformBufferSize;
        sum += m_waveformBuffer[idx] * m_waveformBuffer[idx];
    }
    
    return sqrt(sum / numSamples);
}

std::vector<AudioCapture::AudioDevice> AudioCapture::EnumerateAudioDevices(DeviceType type)
{
    std::vector<AudioDevice> devices;
    HRESULT hr;

    if (!m_deviceEnumerator) {
        hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr,
            CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
            (void**)m_deviceEnumerator.GetAddressOf());
        if (FAILED(hr)) {
            ShowError(L"Failed to create device enumerator", hr);
            return devices;
        }
    }

    // Choose device type
    EDataFlow dataFlow = (type == RenderDevices) ? eRender : eCapture;

    // Enumerate devices
    IMMDeviceCollection* collection = nullptr;
    hr = m_deviceEnumerator->EnumAudioEndpoints(dataFlow, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr)) {
        ShowError(L"Failed to enumerate audio devices", hr);
        return devices;
    }

    UINT count = 0;
    collection->GetCount(&count);

    // Debug output - write to file
    std::wstring debugFileName = (type == RenderDevices) ? L"audio_devices_render_debug.txt" : L"audio_devices_capture_debug.txt";
    HANDLE debugFile = CreateFileW(debugFileName.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (debugFile != INVALID_HANDLE_VALUE) {
        std::string debugText = (type == RenderDevices) ? "Available Render Devices:\n" : "Available Capture Devices:\n";
        DWORD written;
        WriteFile(debugFile, debugText.c_str(), (DWORD)debugText.length(), &written, nullptr);
    }

    for (UINT i = 0; i < count; ++i) {
        IMMDevice* device = nullptr;
        hr = collection->Item(i, &device);
        if (FAILED(hr)) continue;

        // Get device properties
        IPropertyStore* props = nullptr;
        hr = device->OpenPropertyStore(STGM_READ, &props);
        
        std::wstring deviceName = L"Unknown Device";
        
        if (SUCCEEDED(hr)) {
            // Try to get device friendly name
            PROPVARIANT varName;
            PropVariantInit(&varName);
            
            // PKEY_Device_FriendlyName GUID: {a45c254e-df1c-4efd-8020-67d146a850e0}, PID: 14
            PROPERTYKEY keyFriendlyName = { 
                {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 
                14 
            };
            
            hr = props->GetValue(keyFriendlyName, &varName);
            if (SUCCEEDED(hr) && varName.pwszVal) {
                deviceName = varName.pwszVal;
            }
            PropVariantClear(&varName);
        }

        // Get default device to check if this one is default
        IMMDevice* defaultDevice = nullptr;
        bool isDefault = false;
        if (SUCCEEDED(m_deviceEnumerator->GetDefaultAudioEndpoint(dataFlow, eConsole, &defaultDevice))) {
            LPWSTR pszDefaultId = nullptr;
            if (SUCCEEDED(defaultDevice->GetId(&pszDefaultId))) {
                LPWSTR pszCurrentId = nullptr;
                if (SUCCEEDED(device->GetId(&pszCurrentId))) {
                    isDefault = (wcscmp(pszDefaultId, pszCurrentId) == 0);
                    CoTaskMemFree(pszCurrentId);
                }
                CoTaskMemFree(pszDefaultId);
            }
            defaultDevice->Release();
        }

        AudioDevice audioDevice;
        audioDevice.index = devices.size();
        audioDevice.name = deviceName;
        audioDevice.id = L"";
        audioDevice.isDefault = isDefault;

        devices.push_back(audioDevice);

        // Debug output
        if (debugFile != INVALID_HANDLE_VALUE) {
            std::string debugLine = "Device " + std::to_string(i) + ": " + std::string(deviceName.begin(), deviceName.end()) + (isDefault ? " (Default)" : "") + "\n";
            DWORD written;
            WriteFile(debugFile, debugLine.c_str(), (DWORD)debugLine.length(), &written, nullptr);
        }

        if (props) {
            props->Release();
        }
        device->Release();
    }

    if (debugFile != INVALID_HANDLE_VALUE) {
        CloseHandle(debugFile);
    }

    collection->Release();
    return devices;
}

bool AudioCapture::SelectAudioDevice(int deviceIndex, DeviceType type)
{
    try {
        // Wait a bit for capture thread to actually stop (max 100ms with retries)
        for (int retry = 0; retry < 10; retry++) {
            if (!m_isCapturing && !m_isRecording) {
                break;  // Safe to proceed
            }
            Sleep(10);
        }
        
        if (m_isCapturing || m_isRecording) {
            ShowError(L"Cannot select device while capturing or recording", S_OK);
            return false;
        }

        // Get all devices
        std::vector<AudioDevice> devices = EnumerateAudioDevices(type);
        
        if (deviceIndex < 0 || deviceIndex >= (int)devices.size()) {
            ShowError(L"Invalid device index", S_OK);
            return false;
        }

        HRESULT hr;

        if (!m_deviceEnumerator) {
            hr = CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr,
                CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                (void**)m_deviceEnumerator.GetAddressOf());
            if (FAILED(hr)) {
                ShowError(L"Failed to create device enumerator", hr);
                return false;
            }
        }

        // Choose device type
        EDataFlow dataFlow = (type == RenderDevices) ? eRender : eCapture;

        // Get device collection
        IMMDeviceCollection* collection = nullptr;
        hr = m_deviceEnumerator->EnumAudioEndpoints(dataFlow, DEVICE_STATE_ACTIVE, &collection);
        if (FAILED(hr)) {
            ShowError(L"Failed to enumerate audio devices", hr);
            return false;
        }

        // Get specific device
        IMMDevice* device = nullptr;
        hr = collection->Item(deviceIndex, &device);
        collection->Release();

        if (FAILED(hr)) {
            LogError("Failed to get audio device from collection");
            ShowError(L"Failed to get audio device", hr);
            return false;
        }

        // Release old device - do this carefully
        LogError("Resetting audio client components");
        m_captureClient.Reset();
        m_audioClient.Reset();
        m_device.Reset();

        // Set new device
        LogError("Setting new device and reinitializing WASAPI");
        m_device = device;
        m_currentDevice = devices[deviceIndex];
        m_currentDeviceType = type;
        m_deviceSelected = true;

        // Reinitialize with new device
        LogError("Calling InitializeWASAPI for new device");
        if (!InitializeWASAPI()) {
            LogError("InitializeWASAPI failed for new device");
            ShowError(L"Failed to initialize with selected device", S_OK);
            return false;
        }

        LogError("Successfully switched to new audio device");
        return true;
    }
    catch (const std::exception& e) {
        LogError("Exception caught in SelectAudioDevice");
        OutputDebugStringW(L"Exception in SelectAudioDevice\n");
        return false;
    }
}

