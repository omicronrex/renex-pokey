#include <dsound.h>
#include <stdio.h>

#pragma comment(lib, "dsound.lib")

#define WIDE2(x) L##x
#define WIDE1(x) WIDE2(x)
#define WFILE WIDE1(__FILE__)

#define GMREAL extern "C" __declspec(dllexport) double __cdecl
#define GMSTR extern "C" __declspec(dllexport) char* __cdecl

bool __vibe_check(const wchar_t* file, int line, HRESULT hr);
#define vibe_check(a) __vibe_check(WFILE,__LINE__,a)

extern bool __vibe_check(const wchar_t* file, int line, HRESULT hr) {
    if (SUCCEEDED(hr)) return false;    
    wchar_t buf[1024];
    _snwprintf_s(buf, 1024, L"DirectSound error in file %s at line %i:\nHRESULT = 0x%08X",file,line,hr);
    MessageBoxW(0, buf, L"Warning", 0);
    exit(1);
    return true;
}

LPDIRECTSOUND Device;
LPDIRECTSOUNDBUFFER PrimaryBuffer;
LPDIRECTSOUNDBUFFER SecondaryBuffer;
DSBUFFERDESC BufferDescriptor;
WAVEFORMATEX FormatDescriptor;

DSBUFFERDESC* describe_buffer(DWORD flags,WAVEFORMATEX* format,DWORD size) {
    memset(&BufferDescriptor,0,sizeof(BufferDescriptor));
    BufferDescriptor.dwFlags = flags;
    BufferDescriptor.dwBufferBytes = size;
    BufferDescriptor.lpwfxFormat = format;    
    BufferDescriptor.dwReserved = 0;
    BufferDescriptor.dwSize = sizeof(DSBUFFERDESC);
    return &BufferDescriptor;
}

WAVEFORMATEX* describe_format() {
    memset(&FormatDescriptor,0,sizeof(FormatDescriptor));
    FormatDescriptor.wFormatTag = WAVE_FORMAT_PCM;
    FormatDescriptor.nChannels = 1;
    FormatDescriptor.nSamplesPerSec = 22050;
    FormatDescriptor.nBlockAlign = (FormatDescriptor.wBitsPerSample / 8) * FormatDescriptor.nChannels;
    FormatDescriptor.nAvgBytesPerSec = FormatDescriptor.nSamplesPerSec * FormatDescriptor.nBlockAlign;
    FormatDescriptor.cbSize = 0;
    return &FormatDescriptor;
}

GMREAL __pokey_dll_init(HWND hwnd) {
    //initialize diretsound    
    vibe_check(DirectSoundCreate(NULL,&Device,NULL));
    //vibe_check(Device.Initialize(NULL));
    vibe_check(Device->SetCooperativeLevel(hwnd,DSSCL_PRIORITY));    
    
    //create and start the primary buffer
    vibe_check(Device->CreateSoundBuffer(
        describe_buffer(
            DSBCAPS_PRIMARYBUFFER | DSBCAPS_GLOBALFOCUS,
            NULL,
            0
        ),
        &PrimaryBuffer,
        NULL
    ));
    vibe_check(PrimaryBuffer->Play(0,0,DSBPLAY_LOOPING));
    
    //create the secondary buffer to hold our audio frame
    vibe_check(Device->CreateSoundBuffer(
        describe_buffer(
            0,
            describe_format(),
            1024
        ),
        &SecondaryBuffer,
        NULL
    ));
    
    //SecondaryBuffer.Lock(0,)
    
    vibe_check(SecondaryBuffer->Play(0,0,DSBPLAY_LOOPING));
    
    return 0;
}