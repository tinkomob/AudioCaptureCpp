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
    AudioCapture();
    ~AudioCapture();

    bool Initialize();
    bool StartCapture();
    bool StopCapture();
    bool StartRecording(const wchar_t* filename);
    bool StopRecording();
    bool IsRecording() const { return m_isRecording; }
    bool IsCapturing() const { return m_isCapturing; }
    
    // Get waveform buffer for visualization
    std::vector<float> GetWaveformBuffer() const;
    float GetCurrentLevel() const;
    int GetSampleCount() const { return m_sampleCount; }

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

    // Capture state
    bool m_isCapturing = false;
    std::unique_ptr<std::thread> m_captureThread;

    // Recording state
    bool m_isRecording = false;
    std::unique_ptr<std::thread> m_recordingThread;
    mutable std::mutex m_mutex;
    
    // File handling
    HANDLE m_audioFile = INVALID_HANDLE_VALUE;
    DWORD m_bytesWritten = 0;
    
    // Audio data
    std::vector<float> m_waveformBuffer;
    int m_waveformPos = 0;
    int m_sampleCount = 0;
    
    // Audio format
    WAVEFORMATEX m_waveFormat = {};
    UINT32 m_bufferFrameCount = 0;
};
