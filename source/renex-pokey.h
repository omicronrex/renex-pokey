#include <windows.h>
#include <dsound.h>
#include <stdio.h>

#pragma comment(lib,"dsound.lib")
#pragma comment(lib,"User32.lib")

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
    _snwprintf_s(buf, 1024, L"DirectSound error in file %s at line %i:\nHRESULT = 0x%08X", file, line, hr);
    MessageBoxW(0, buf, L"Warning", 0);
    exit(1);
    return true;
}


//-------------------------------------------------------------------------//


LPDIRECTSOUND Device;
LPDIRECTSOUNDBUFFER PrimaryBuffer;
LPDIRECTSOUNDBUFFER SecondaryBuffer;
DSBUFFERDESC BufferDescriptor;
WAVEFORMATEX FormatDescriptor;
unsigned char* TertiaryBuffer;
int buffer_length;

void* lock_chunk1;
DWORD lock_size1;
void* lock_chunk2;
DWORD lock_size2;


//-------------------------------------------------------------------------//


int secondary_buffer_lock();
void secondary_buffer_fill();
void timer_callback(HWND a, UINT b, UINT_PTR c, DWORD ms);

void pokey_startup();
void pokey_engine(int amount);


//-------------------------------------------------------------------------//


DSBUFFERDESC* describe_buffer(DWORD flags,WAVEFORMATEX* format,DWORD size) {
    memset(&BufferDescriptor,0,sizeof(BufferDescriptor));
    BufferDescriptor.dwFlags = flags;
    BufferDescriptor.dwBufferBytes = size;
    BufferDescriptor.lpwfxFormat = format;    
    BufferDescriptor.dwReserved = 0;
    BufferDescriptor.dwSize = sizeof(DSBUFFERDESC);
    return &BufferDescriptor;
}

WAVEFORMATEX* describe_format(int sample_rate) {
    memset(&FormatDescriptor,0,sizeof(FormatDescriptor));
    FormatDescriptor.wFormatTag = WAVE_FORMAT_PCM;
    FormatDescriptor.nChannels = 1;
    FormatDescriptor.nSamplesPerSec = (DWORD)sample_rate;
    FormatDescriptor.wBitsPerSample = 8;
    FormatDescriptor.nBlockAlign = (FormatDescriptor.wBitsPerSample / 8) * FormatDescriptor.nChannels;
    FormatDescriptor.nAvgBytesPerSec = FormatDescriptor.nSamplesPerSec * FormatDescriptor.nBlockAlign;
    FormatDescriptor.cbSize = 0;
    return &FormatDescriptor;
}
