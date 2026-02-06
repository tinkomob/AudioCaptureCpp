#include <windows.h>
#include <string>
#include <sstream>
#include <thread>
#include "audio_capture.h"

// Global variables
HWND hwndMainWindow;
HWND hwndStatusLabel;
HWND hwndSampleCountLabel;
HWND hwndStartButton;
HWND hwndStopButton;
HWND hwndWaveformCanvas;

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

    if (g_audioCapture.StartRecording(filename.c_str())) {
        g_isRecording = true;
        SetWindowTextW(hwndStatusLabel, L"Status: Recording...");
        EnableWindow(hwndStartButton, FALSE);
        EnableWindow(hwndStopButton, TRUE);
    }
}

void StopRecording() {
    if (!g_isRecording) return;

    g_isRecording = false;
    g_audioCapture.StopRecording();

    SetWindowTextW(hwndStatusLabel, L"Status: Ready");
    EnableWindow(hwndStartButton, TRUE);
    EnableWindow(hwndStopButton, FALSE);
}

void DrawAudioTrack(HDC hdc, float level, int width, int height) {
    // Clear background
    HBRUSH bgBrush = CreateSolidBrush(RGB(30, 30, 30));
    RECT rect = {0, 0, width, height};
    FillRect(hdc, &rect, bgBrush);
    DeleteObject(bgBrush);

    // Draw level bar
    int barWidth = 50;
    int barHeight = height - 20;
    int barX = (width - barWidth) / 2;
    int barY = 10;

    // Background bar
    HBRUSH barBgBrush = CreateSolidBrush(RGB(50, 50, 50));
    RECT barRect = {barX, barY, barX + barWidth, barY + barHeight};
    FillRect(hdc, &barRect, barBgBrush);
    DeleteObject(barBgBrush);

    // Level bar
    int levelHeight = (int)(level * barHeight);
    HBRUSH levelBrush = CreateSolidBrush(RGB(0, 255, 0));
    RECT levelRect = {barX, barY + barHeight - levelHeight, barX + barWidth, barY + barHeight};
    FillRect(hdc, &levelRect, levelBrush);
    DeleteObject(levelBrush);

    // Draw border
    HPEN pen = CreatePen(PS_SOLID, 2, RGB(100, 100, 100));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, barX, barY, nullptr);
    LineTo(hdc, barX + barWidth, barY);
    LineTo(hdc, barX + barWidth, barY + barHeight);
    LineTo(hdc, barX, barY + barHeight);
    LineTo(hdc, barX, barY);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

LRESULT CALLBACK CanvasWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            RECT rect;
            GetClientRect(hwnd, &rect);
            int width = rect.right - rect.left;
            int height = rect.bottom - rect.top;

            float level = g_audioCapture.GetCurrentLevel();
            DrawAudioTrack(hdc, level, width, height);

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

            // Waveform canvas
            WNDCLASSW wc = {};
            wc.lpfnWndProc = CanvasWndProc;
            wc.lpszClassName = L"WaveformCanvas";
            wc.hbrBackground = CreateSolidBrush(RGB(30, 30, 30));
            RegisterClassW(&wc);

            hwndWaveformCanvas = CreateWindowW(L"WaveformCanvas", nullptr,
                WS_CHILD | WS_VISIBLE, 10, 100, 780, 250, hwnd, nullptr, nullptr, nullptr);

            // Start button
            hwndStartButton = CreateWindowW(L"BUTTON", L"Start Recording",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 10, 360, 120, 30,
                hwnd, (HMENU)1, nullptr, nullptr);

            // Stop button
            hwndStopButton = CreateWindowW(L"BUTTON", L"Stop Recording",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED, 140, 360, 120, 30,
                hwnd, (HMENU)2, nullptr, nullptr);

            // Exit button
            CreateWindowW(L"BUTTON", L"Close",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 270, 360, 120, 30,
                hwnd, (HMENU)3, nullptr, nullptr);

            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wParam);
            switch (id) {
                case 1: StartRecording(); break;
                case 2: StopRecording(); break;
                case 3: PostMessage(hwnd, WM_CLOSE, 0, 0); break;
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

    // Start UI update thread
    std::thread uiThread([]() {
        while (true) {
            int samples = g_audioCapture.GetSampleCount();
            std::wstring text = L"Samples: ";
            text += std::to_wstring(samples);
            SetWindowTextW(hwndSampleCountLabel, text.c_str());

            InvalidateRect(hwndWaveformCanvas, nullptr, FALSE);
            Sleep(100);
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
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 450,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwndMainWindow) {
        MessageBoxW(nullptr, L"Failed to create window", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwndMainWindow, nCmdShow);
    UpdateWindow(hwndMainWindow);

    // Message loop
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return (int)msg.wParam;
}
