#include <windows.h>
#include <string>
#include <sstream>
#include <thread>
#include <stdexcept>
#include <vector>
#include "audio_capture.h"

// Global variables
HWND hwndMainWindow;
HWND hwndStatusLabel;
HWND hwndSampleCountLabel;
HWND hwndStartButton;
HWND hwndStopButton;
HWND hwndWaveformCanvas;
HWND hwndDeviceLabel;
HWND hwndDeviceCombo;
HWND hwndCurrentDeviceLabel;
HWND hwndRenderRadio;
HWND hwndCaptureRadio;

AudioCapture g_audioCapture;
bool g_isRecording = false;
int g_recordingCount = 0;

// Forward declarations
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK CanvasWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void StartRecording() {
    if (g_isRecording) return;

    g_recordingCount++;
    std::wstring filename = L"recording_";
    filename += std::to_wstring(g_recordingCount);
    filename += L".wav";

    if (g_audioCapture.StartCapture() && g_audioCapture.StartRecording(filename.c_str())) {
        g_isRecording = true;
        SetWindowTextW(hwndStatusLabel, L"Status: Recording...");
        SetWindowTextW(hwndStopButton, L"Stop Recording");
    }
}

void StopRecording() {
    if (!g_isRecording) return;

    g_isRecording = false;
    g_audioCapture.StopRecording();
    g_audioCapture.StopCapture();

    SetWindowTextW(hwndStatusLabel, L"Status: Stopped");
    SetWindowTextW(hwndStopButton, L"Resume Recording");
}

void ToggleRecording() {
    if (g_isRecording) {
        StopRecording();
    } else {
        StartRecording();
    }
}

void RefreshDeviceList() {
    // Clear existing items
    SendMessageW(hwndDeviceCombo, CB_RESETCONTENT, 0, 0);

    // Stop recording and capturing before switching device types
    bool wasRecording = g_isRecording;
    bool wasCapturing = g_audioCapture.IsCapturing();
    
    if (wasRecording) {
        StopRecording();
        Sleep(100);
    }
    
    if (wasCapturing) {
        g_audioCapture.StopCapture();
        Sleep(100);
    }

    // Determine device type
    AudioCapture::DeviceType deviceType = AudioCapture::RenderDevices;
    if (SendMessageW(hwndCaptureRadio, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        deviceType = AudioCapture::CaptureDevices;
    }

    // Get available devices
    auto devices = g_audioCapture.EnumerateAudioDevices(deviceType);

    // Add devices to combo box
    for (const auto& device : devices) {
        std::wstring itemText = device.name;
        if (device.isDefault) {
            itemText += L" (Default)";
        }
        SendMessageW(hwndDeviceCombo, CB_ADDSTRING, 0, (LPARAM)itemText.c_str());
    }

    // Select first device
    if (SendMessageW(hwndDeviceCombo, CB_GETCOUNT, 0, 0) > 0) {
        SendMessageW(hwndDeviceCombo, CB_SETCURSEL, 0, 0);
    }
    
    // Resume capturing and recording if they were active
    if (wasCapturing) {
        if (!g_audioCapture.StartCapture()) {
            MessageBoxW(hwndMainWindow, L"Failed to restart audio capture", L"Error", MB_OK | MB_ICONERROR);
        }
    }
    
    if (wasRecording && wasCapturing) {
        StartRecording();
    }
}

void SelectAudioDevice() {
    int selectedIndex = (int)SendMessageW(hwndDeviceCombo, CB_GETCURSEL, 0, 0);
    if (selectedIndex == CB_ERR || selectedIndex < 0) {
        return; // No device selected
    }

    // Determine device type
    AudioCapture::DeviceType deviceType = AudioCapture::RenderDevices;
    if (SendMessageW(hwndCaptureRadio, BM_GETCHECK, 0, 0) == BST_CHECKED) {
        deviceType = AudioCapture::CaptureDevices;
    }

    // Need to stop recording and capturing first to change device
    bool wasRecording = g_isRecording;
    bool wasCapturing = g_audioCapture.IsCapturing();
    
    if (wasRecording) {
        StopRecording();
        Sleep(100);  // Give it time to stop
    }
    
    if (wasCapturing) {
        g_audioCapture.StopCapture();
        Sleep(100);  // Give it time to stop
    }

    try {
        // Select the device
        if (g_audioCapture.SelectAudioDevice(selectedIndex, deviceType)) {
            // Update current device label
            auto device = g_audioCapture.GetCurrentDevice();
            std::wstring labelText = L"Current: ";
            labelText += device.name;
            labelText += (deviceType == AudioCapture::RenderDevices) ? L" (Playback)" : L" (Recording)";
            SetWindowTextW(hwndCurrentDeviceLabel, labelText.c_str());
            
            // Resume capturing if it was active
            if (wasCapturing) {
                if (!g_audioCapture.StartCapture()) {
                    MessageBoxW(hwndMainWindow, L"Failed to restart audio capture", L"Error", MB_OK | MB_ICONERROR);
                }
            }
            
            // Resume recording if it was active
            if (wasRecording && wasCapturing) {
                StartRecording();
            }
        } else {
            MessageBoxW(hwndMainWindow, L"Failed to select audio device", L"Error", MB_OK | MB_ICONERROR);
            
            // Try to resume with previous device
            if (wasCapturing) {
                g_audioCapture.StartCapture();
            }
            if (wasRecording && wasCapturing) {
                StartRecording();
            }
        }
    }
    catch (const std::exception& e) {
        MessageBoxW(hwndMainWindow, L"Exception while selecting device", L"Error", MB_OK | MB_ICONERROR);
    }
}

void DrawAudioTrack(HDC hdc, float level, int width, int height) {
    // Create memory DC for double buffering (prevents flickering)
    HDC memDC = CreateCompatibleDC(hdc);
    HBITMAP memBitmap = CreateCompatibleBitmap(hdc, width, height);
    HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
    
    // Clear background
    HBRUSH bgBrush = CreateSolidBrush(RGB(30, 30, 30));
    RECT rect = {0, 0, width, height};
    FillRect(memDC, &rect, bgBrush);
    DeleteObject(bgBrush);

    // Draw volume bar at top (40 pixels high)
    int barWidth = 50;
    int barHeight = 30;
    int barX = (width - barWidth) / 2;
    int barY = 5;

    // Background bar
    HBRUSH barBgBrush = CreateSolidBrush(RGB(50, 50, 50));
    RECT barRect = {barX, barY, barX + barWidth, barY + barHeight};
    FillRect(memDC, &barRect, barBgBrush);
    DeleteObject(barBgBrush);

    // Level bar
    int levelHeight = (int)(level * barHeight);
    HBRUSH levelBrush = CreateSolidBrush(RGB(0, 255, 0));
    RECT levelRect = {barX, barY + barHeight - levelHeight, barX + barWidth, barY + barHeight};
    FillRect(memDC, &levelRect, levelBrush);
    DeleteObject(levelBrush);

    // Draw border
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(100, 100, 100));
    HPEN oldPen = (HPEN)SelectObject(memDC, pen);
    MoveToEx(memDC, barX, barY, nullptr);
    LineTo(memDC, barX + barWidth, barY);
    LineTo(memDC, barX + barWidth, barY + barHeight);
    LineTo(memDC, barX, barY + barHeight);
    LineTo(memDC, barX, barY);
    SelectObject(memDC, oldPen);
    DeleteObject(pen);

    // Draw waveform at bottom (remaining height)
    int waveformY = barY + barHeight + 5;
    int waveformHeight = height - waveformY - 5;
    
    if (waveformHeight > 10) {
        int pixelWidth = width - 20;
        
        // Quick copy of waveform data with minimal lock time
        std::vector<float> waveformCopy;
        int waveformPos = 0;
        int bufferSize = 0;
        {
            std::lock_guard<std::mutex> lock(g_audioCapture.GetWaveformMutex());
            const auto& waveformBuffer = g_audioCapture.GetWaveformBufferRef();
            bufferSize = g_audioCapture.GetWaveformBufferSize();
            waveformPos = g_audioCapture.GetWaveformPosition();
            
            // Copy only the data we need to display (much faster)
            int samplesPerPixel = max(1, bufferSize / max(1, pixelWidth));
            int totalSamplesToShow = min(pixelWidth * samplesPerPixel, bufferSize);
            waveformCopy.reserve(totalSamplesToShow);
            
            int startPos = (waveformPos - totalSamplesToShow + bufferSize) % bufferSize;
            for (int i = 0; i < totalSamplesToShow; i++) {
                int idx = (startPos + i) % bufferSize;
                waveformCopy.push_back(waveformBuffer[idx]);
            }
        }
        // Lock released - now draw without blocking audio threads
        
        if (!waveformCopy.empty()) {
            int centerY = waveformY + waveformHeight / 2;
            int samplesPerPixel = max(1, (int)waveformCopy.size() / pixelWidth);
            
            // Create waveform pen once
            HPEN waveformPen = CreatePen(PS_SOLID, 1, RGB(0, 200, 100));
            HPEN oldWaveformPen = (HPEN)SelectObject(memDC, waveformPen);
            
            // Draw waveform lines (top half)
            bool first = true;
            for (int x = 0; x < pixelWidth && x < width - 10; x++) {
                float maxSample = 0.0f;
                int startIdx = x * samplesPerPixel;
                int endIdx = min(startIdx + samplesPerPixel, (int)waveformCopy.size());
                
                for (int i = startIdx; i < endIdx; i++) {
                    maxSample = max(maxSample, fabs(waveformCopy[i]));
                }
                
                int barHeight = (int)(maxSample * (waveformHeight / 2 - 2));
                barHeight = max(0, min(waveformHeight / 2 - 2, barHeight));
                
                if (first) {
                    MoveToEx(memDC, x + 10, centerY - barHeight, nullptr);
                    first = false;
                } else {
                    LineTo(memDC, x + 10, centerY - barHeight);
                }
            }
            
            // Draw bottom half
            first = true;
            for (int x = 0; x < pixelWidth && x < width - 10; x++) {
                float maxSample = 0.0f;
                int startIdx = x * samplesPerPixel;
                int endIdx = min(startIdx + samplesPerPixel, (int)waveformCopy.size());
                
                for (int i = startIdx; i < endIdx; i++) {
                    maxSample = max(maxSample, fabs(waveformCopy[i]));
                }
                
                int barHeight = (int)(maxSample * (waveformHeight / 2 - 2));
                barHeight = max(0, min(waveformHeight / 2 - 2, barHeight));
                
                if (first) {
                    MoveToEx(memDC, x + 10, centerY + barHeight, nullptr);
                    first = false;
                } else {
                    LineTo(memDC, x + 10, centerY + barHeight);
                }
            }
            
            SelectObject(memDC, oldWaveformPen);
            DeleteObject(waveformPen);
            
            // Draw waveform border
            HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
            HPEN oldBorderPen = (HPEN)SelectObject(memDC, borderPen);
            MoveToEx(memDC, 10, waveformY, nullptr);
            LineTo(memDC, width - 10, waveformY);
            LineTo(memDC, width - 10, waveformY + waveformHeight);
            LineTo(memDC, 10, waveformY + waveformHeight);
            LineTo(memDC, 10, waveformY);
            SelectObject(memDC, oldBorderPen);
            DeleteObject(borderPen);
            
            // Draw center line
            HPEN centerPen = CreatePen(PS_SOLID, 1, RGB(50, 50, 50));
            HPEN oldCenterPen = (HPEN)SelectObject(memDC, centerPen);
            MoveToEx(memDC, 10, centerY, nullptr);
            LineTo(memDC, width - 10, centerY);
            SelectObject(memDC, oldCenterPen);
            DeleteObject(centerPen);
        }
    }
    
    // Copy mem DC to screen (fast blit, no flickering)
    BitBlt(hdc, 0, 0, width, height, memDC, 0, 0, SRCCOPY);
    
    // Cleanup
    SelectObject(memDC, oldBitmap);
    DeleteObject(memBitmap);
    DeleteDC(memDC);
}

LRESULT CALLBACK CanvasWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            try {
                RECT rect;
                GetClientRect(hwnd, &rect);
                int width = rect.right - rect.left;
                int height = rect.bottom - rect.top;

                float level = g_audioCapture.GetCurrentLevel();
                DrawAudioTrack(hdc, level, width, height);
            }
            catch (...) {
                // If drawing fails, just fill with black
                RECT rect;
                GetClientRect(hwnd, &rect);
                HBRUSH brush = CreateSolidBrush(RGB(30, 30, 30));
                FillRect(hdc, &rect, brush);
                DeleteObject(brush);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            // Title
            CreateWindowW(L"STATIC", L"Real-time System Audio Recorder",
                WS_CHILD | WS_VISIBLE, 10, 10, 400, 30, hwnd, nullptr, nullptr, nullptr);

            // Status label
            hwndStatusLabel = CreateWindowW(L"STATIC", L"Status: Ready",
                WS_CHILD | WS_VISIBLE, 10, 40, 400, 25, hwnd, nullptr, nullptr, nullptr);

            // Sample count label
            hwndSampleCountLabel = CreateWindowW(L"STATIC", L"Samples: 0",
                WS_CHILD | WS_VISIBLE, 10, 70, 400, 25, hwnd, nullptr, nullptr, nullptr);

            // Device label
            hwndDeviceLabel = CreateWindowW(L"STATIC", L"Audio Device:",
                WS_CHILD | WS_VISIBLE, 10, 100, 80, 25, hwnd, nullptr, nullptr, nullptr);

            // Device type radio buttons
            hwndRenderRadio = CreateWindowW(L"BUTTON", L"Playback (Loopback)",
                WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                10, 125, 140, 25, hwnd, (HMENU)6, nullptr, nullptr);
            hwndCaptureRadio = CreateWindowW(L"BUTTON", L"Recording (Direct)",
                WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                160, 125, 140, 25, hwnd, (HMENU)7, nullptr, nullptr);

            // Set default selection
            SendMessageW(hwndRenderRadio, BM_SETCHECK, BST_CHECKED, 0);

            // Device combo box
            hwndDeviceCombo = CreateWindowW(L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST, 100, 150, 350, 200,
                hwnd, (HMENU)4, nullptr, nullptr);

            // Current device label
            hwndCurrentDeviceLabel = CreateWindowW(L"STATIC", L"Current: Default System Device",
                WS_CHILD | WS_VISIBLE, 10, 180, 400, 25, hwnd, nullptr, nullptr, nullptr);

            // Waveform canvas
            WNDCLASSW wc = {};
            wc.lpfnWndProc = CanvasWndProc;
            wc.lpszClassName = L"WaveformCanvas";
            wc.hbrBackground = CreateSolidBrush(RGB(30, 30, 30));
            RegisterClassW(&wc);

            hwndWaveformCanvas = CreateWindowW(L"WaveformCanvas", nullptr,
                WS_CHILD | WS_VISIBLE, 10, 210, 980, 300, hwnd, nullptr, nullptr, nullptr);

            // Stop/Resume button (centered)
            hwndStopButton = CreateWindowW(L"BUTTON", L"Stop Recording",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 440, 520, 120, 30,
                hwnd, (HMENU)2, nullptr, nullptr);

            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            int code = HIWORD(wParam);
            
            switch (id) {
                case 2: ToggleRecording(); break;
                case 4: // Device combo box
                    if (code == CBN_SELCHANGE) {
                        SelectAudioDevice();
                    }
                    break;
                case 6: // Render devices radio button
                case 7: // Capture devices radio button
                    if (code == BN_CLICKED) {
                        RefreshDeviceList();
                        // Small delay to ensure list is populated
                        Sleep(50);
                        // Auto-select first device
                        SelectAudioDevice();
                    }
                    break;
            }
            return 0;
        }

        case WM_CLOSE: {
            StopRecording();
            g_audioCapture.StopCapture();
            DestroyWindow(hwnd);
            return 0;
        }

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR pCmdLine, int nCmdShow) {
    try {
    // Initialize audio capture
    if (!g_audioCapture.Initialize()) {
        MessageBoxW(nullptr, L"Failed to initialize audio capture", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Start capturing for real-time visualization
    if (!g_audioCapture.StartCapture()) {
        MessageBoxW(nullptr, L"Failed to start audio capture", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Start UI update thread with higher frequency for smooth waveform
    std::thread uiThread([]() {
        while (true) {
            int samples = g_audioCapture.GetSampleCount();
            std::wstring text = L"Samples: ";
            text += std::to_wstring(samples);
            SetWindowTextW(hwndSampleCountLabel, text.c_str());

            InvalidateRect(hwndWaveformCanvas, nullptr, FALSE);
            Sleep(33);  // ~30 FPS for smooth real-time visualization
        }
    });
    uiThread.detach();

    // Register window class
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"AudioCaptureWindow";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(50, 50, 50));

    if (!RegisterClassW(&wc)) {
        MessageBoxW(nullptr, L"Failed to register window class", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Create main window
    hwndMainWindow = CreateWindowW(
        L"AudioCaptureWindow",
        L"Audio Capture - System Audio Recorder",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 1000, 600,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwndMainWindow) {
        MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwndMainWindow, nCmdShow);
    UpdateWindow(hwndMainWindow);

    // Refresh device list
    RefreshDeviceList();
    
    // Update current device label
    auto currentDevice = g_audioCapture.GetCurrentDevice();
    auto currentType = g_audioCapture.GetCurrentDeviceType();
    std::wstring labelText = L"Current: ";
    labelText += currentDevice.name;
    labelText += (currentType == AudioCapture::RenderDevices) ? L" (Playback)" : L" (Recording)";
    SetWindowTextW(hwndCurrentDeviceLabel, labelText.c_str());

    // Start recording automatically
    StartRecording();

    // Message loop
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
    } // end try
    catch (const std::exception& e) {
        std::string msg = "Exception in wWinMain: ";
        msg += e.what();
        MessageBoxA(nullptr, msg.c_str(), "Fatal Error", MB_OK | MB_ICONERROR);
        return 2;
    }
    catch (...) {
        MessageBoxW(nullptr, L"Unknown exception in wWinMain", L"Fatal Error", MB_OK | MB_ICONERROR);
        return 3;
    }
}
