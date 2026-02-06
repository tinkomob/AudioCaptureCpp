#include "audio_capture.h"
#include <mmsystem.h>
#include <chrono>
#include <string>
#include <cmath>

#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")

const int WAVEFORM_BUFFER_SIZE = 2048;
const REFERENCE_TIME REFTIMES_PER_SEC = 10000000;
const REFERENCE_TIME REFTIMES_PER_MILLISEC = 10000;

void ShowError(const wchar_t* message, HRESULT hr) {
    wchar_t buffer[256];
    swprintf_s(buffer, L"%s\nHRESULT: 0x%08X", message, hr);
    MessageBoxW(nullptr, buffer, L"Audio Capture Error", MB_OK | MB_ICONERROR);
}

AudioCapture::AudioCapture()
{
    m_waveformBuffer.resize(WAVEFORM_BUFFER_SIZE, 0.0f);
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

    m_isCapturing = false;
    if (m_captureThread && m_captureThread->joinable()) {
        m_captureThread->join();
    }
    m_captureThread.reset();

    m_audioClient->Stop();
    return true;
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

    // Get default audio endpoint (loopback for system audio capture)
    // For system audio, we need to get the render device and use loopback
    hr = m_deviceEnumerator->GetDefaultAudioEndpoint(
        eRender, eConsole, m_device.GetAddressOf());
    if (FAILED(hr)) {
        ShowError(L"Failed to get default audio endpoint", hr);
        return false;
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

    // Initialize audio client for capture (loopback)
    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_LOOPBACK,
        REFTIMES_PER_SEC,
        0,
        &m_waveFormat,
        nullptr);
    if (FAILED(hr)) {
        ShowError(L"Failed to initialize audio client for loopback capture", hr);
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

    // Start audio client
    HRESULT hr = m_audioClient->Start();
    if (FAILED(hr)) {
        m_isRecording = false;
        return false;
    }

    // Start recording thread
    m_recordingThread = std::make_unique<std::thread>([this]() { RecordingThread(); });

    return true;
}

bool AudioCapture::StopRecording()
{
    if (!m_isRecording) return false;

    m_isRecording = false;

    // Stop audio client
    if (m_audioClient) {
        m_audioClient->Stop();
    }

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
                            
                            // Shift waveform buffer
                            for (int j = 0; j < WAVEFORM_BUFFER_SIZE - 1; j++) {
                                m_waveformBuffer[j] = m_waveformBuffer[j + 1];
                            }
                            m_waveformBuffer[WAVEFORM_BUFFER_SIZE - 1] = sample;
                            
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

    while (m_isCapturing) {
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

            if (!(streamFlags & AUDCLNT_BUFFERFLAGS_SILENT)) {
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
            }

            m_captureClient->ReleaseBuffer(numFramesAvailable);

            hr = m_captureClient->GetNextPacketSize(&nextPacketSize);
            if (FAILED(hr)) break;
        }
    }
}

std::vector<float> AudioCapture::GetWaveformBuffer() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_waveformBuffer;
}

float AudioCapture::GetCurrentLevel() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_waveformBuffer.empty()) return 0.0f;
    
    // Calculate RMS of last 100 samples
    const int numSamples = min(100, (int)m_waveformBuffer.size());
    float sum = 0.0f;
    for (int i = m_waveformBuffer.size() - numSamples; i < m_waveformBuffer.size(); ++i) {
        sum += m_waveformBuffer[i] * m_waveformBuffer[i];
    }
    return sqrt(sum / numSamples);
}
