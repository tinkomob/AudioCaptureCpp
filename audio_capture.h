#pragma once

#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl/client.h>
#include <vector>
#include <thread>
#include <mutex>
#include <memory>

using Microsoft::WRL::ComPtr;

class AudioCapture
{
public:
    enum DeviceType {
        RenderDevices,  // Speakers/Headphones (for loopback capture)
        CaptureDevices  // Microphones (for direct capture)
    };

    struct AudioDevice
    {
        int index;
        std::wstring name;
        std::wstring id;
        bool isDefault;
    };

    AudioCapture();
    ~AudioCapture();

    bool Initialize();
    bool StartCapture();
    bool StopCapture();
    bool StartRecording(const wchar_t* filename);
    bool StopRecording();
    bool IsRecording() const { return m_isRecording; }
    bool IsCapturing() const { return m_isCapturing; }
    
    // Device enumeration
    std::vector<AudioDevice> EnumerateAudioDevices(DeviceType type = RenderDevices);
    bool SelectAudioDevice(int deviceIndex, DeviceType type = RenderDevices);
    AudioDevice GetCurrentDevice() const { return m_currentDevice; }
    DeviceType GetCurrentDeviceType() const { return m_currentDeviceType; }
    
    // Get waveform buffer for visualization (use with GetWaveformBufferAccessor)
    std::mutex& GetWaveformMutex() { return m_mutex; }
    const std::vector<float>& GetWaveformBufferRef() const { return m_waveformBuffer; }
    float GetCurrentLevel() const;
    int GetSampleCount() const { return m_sampleCount; }
    int GetWaveformPosition() const { return m_waveformPos; }
    int GetWaveformBufferSize() const { return m_waveformBufferSize; }

private:
    bool InitializeWASAPI();
    void CaptureThread();
    void RecordingThread();
    void WriteWaveHeader();
    void UpdateWaveHeader();
    void ProcessAudioData();

    // WASAPI interfaces
    ComPtr<IMMDeviceEnumerator> m_deviceEnumerator;
    ComPtr<IMMDevice> m_device;
    ComPtr<IAudioClient> m_audioClient;
    ComPtr<IAudioCaptureClient> m_captureClient;

    // Current selected device
    AudioDevice m_currentDevice = {};
    DeviceType m_currentDeviceType = RenderDevices;
    bool m_deviceSelected = false;

    // Capture state
    bool m_isCapturing = false;
    bool m_stopCapture = false;  // Signal to stop capture thread
    std::unique_ptr<std::thread> m_captureThread;

    // Recording state
    bool m_isRecording = false;
    std::unique_ptr<std::thread> m_recordingThread;
    mutable std::mutex m_mutex;
    
    // File handling
    HANDLE m_audioFile = INVALID_HANDLE_VALUE;
    DWORD m_bytesWritten = 0;
    
    // Audio data (45 seconds at 48kHz = 2,160,000 samples)
    std::vector<float> m_waveformBuffer;
    int m_waveformPos = 0;
    int m_sampleCount = 0;
    int m_waveformBufferSize = 0;  // Actual size in samples
    
    // Audio format
    WAVEFORMATEX m_waveFormat = {};
    UINT32 m_bufferFrameCount = 0;
};
