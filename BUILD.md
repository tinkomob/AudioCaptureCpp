# Audio Capture C++ - Инструкции по сборке

## Требования

- Windows 10 или выше
- Visual Studio 2022/2026 или Build Tools for Visual Studio
- Windows 10/11 SDK
- CMake 3.15 или выше

## Способ 1: Visual Studio (Рекомендуется)

1. Откройте Visual Studio
2. File → Open → Project/Solution
3. Выберите `AudioCapture.vcxproj`
4. Нажмите Build → Build Solution (или Ctrl+Shift+B)
5. Exe файл будет находиться в `x64\Release` или `Win32\Release`

## Способ 2: Command Line (MSBuild)

```cmd
cd C:\Users\intel\Desktop\audio-cap\AudioCaptureCpp

# Для 64-bit Release
msbuild AudioCapture.vcxproj /p:Configuration=Release /p:Platform=x64

# Для 32-bit Release
msbuild AudioCapture.vcxproj /p:Configuration=Release /p:Platform=Win32

# Для Debug (с информацией отладки)
msbuild AudioCapture.vcxproj /p:Configuration=Debug /p:Platform=x64
```

## Способ 3: CMake (Рекомендуется для кросс-платформенности)

```cmd
cd C:\Users\intel\Desktop\audio-cap\AudioCaptureCpp

# Создать директорию сборки
mkdir build
cd build

# Сгенерировать проект (автоматически определяет версию VS)
cmake ..

# Или явно указать генератор:
cmake -G "Visual Studio 18 2026" -A x64 ..

# Собрать
cmake --build . --config Release

# Exe будет в build\Release\
```

## Запуск

После сборки просто запустите exe файл двойным кликом:

```
AudioCaptureCpp.exe
```

Или из командной строки:

```cmd
.\AudioCaptureCpp.exe
```

## Использование

1. Нажмите "Start Recording" чтобы начать захват аудио
2. Смотрите обновление счетчика образцов в реальном времени
3. Waveform отображается ниже
4. Нажмите "Stop Recording" чтобы сохранить WAV файл
5. Файлы сохраняются как `recording_1.wav`, `recording_2.wav` и т.д.

## Проблемы при сборке

### Ошибка: "Failed to initialize audio client for loopback capture" (HRESULT: 0x80070057)

Эта ошибка возникает из-за неправильного формата аудио при инициализации WASAPI loopback захвата. Исправлено в версии 1.1 - теперь используется стандартный 16-bit PCM формат вместо сырого формата микширования устройства.

### Ошибка: "Cannot find 'msbuild'"

Добавьте путь к MSBuild в PATH:
```cmd
set PATH=%PATH%;C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin
```

### Ошибка: Missing Windows SDK

Установите Windows 10/11 SDK через Visual Studio Installer.

### Ошибка: LNK1104 - cannot open file

Убедитесь, что у вас установлены все необходимые Windows libraries:
- ole32.lib
- mmdevapi.lib  
- winmm.lib

Если exe файл заблокирован, закройте приложение перед пересборкой.

## Получение exe файла

После успешной сборки exe находится здесь:

**Через Visual Studio:**
```
AudioCaptureCpp\x64\Release\AudioCaptureCpp.exe
```

**Через CMake:**
```
AudioCaptureCpp\build\Release\AudioCaptureCpp.exe
```

или для 32-bit:
```
AudioCaptureCpp\build\Win32\Release\AudioCaptureCpp.exe
```

## Важные замечания

⚠️ **Требуется запуск в режиме администратора** на некоторых версиях Windows  
⚠️ **Требуется включенная система воспроизведения звука**  
⚠️ **Некоторые приложения могут блокировать loopback захват**
