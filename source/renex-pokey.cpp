#include <windows.h>
#include <dsound.h>
#include <stdio.h>

#pragma comment(lib,"dsound.lib")
#pragma comment(lib,"User32.lib")

#define GMREAL extern "C" __declspec(dllexport) double __cdecl
#define GMSTR extern "C" __declspec(dllexport) char* __cdecl


//-------------------------------------------------------------------------//
//debug helpers 🖐


#define WIDE2(x) L##x
#define WIDE1(x) WIDE2(x)

bool __vibe_check(const wchar_t* file, int line, HRESULT hr);
#define vibe_check(a) __vibe_check(WIDE1(__FILE__),__LINE__,a)
extern bool __vibe_check(const wchar_t* file, int line, HRESULT hr) {
    if (SUCCEEDED(hr)) return false;
    wchar_t buf[1024];
    _snwprintf_s(
        buf, 1024,
        L"DirectSound error in file %s at line %i:\nHRESULT = 0x%08X",
        file, line, hr
    );
    MessageBoxW(0, buf, L"Warning", 0);
    exit(1);
    return true;
}


//-------------------------------------------------------------------------//
//global variables


LPDIRECTSOUND Device;
LPDIRECTSOUNDBUFFER PrimaryBuffer;
LPDIRECTSOUNDBUFFER SecondaryBuffer;
DSBUFFERDESC BufferDescriptor;
WAVEFORMATEX FormatDescriptor;
unsigned char* TertiaryBuffer;

void* lock_chunk1;
DWORD lock_size1;
void* lock_chunk2;
DWORD lock_size2;

int buffer_length;


//-------------------------------------------------------------------------//
//function prototypes

void dll_init(HWND hwnd,int samplerate);
void timer_callback(HWND a, UINT b, UINT_PTR c, DWORD ms);

int secondary_buffer_lock();
void secondary_buffer_fill();

void pokey_init();
void pokey_engine(int amount);


//-------------------------------------------------------------------------//
//helpers for annoying struct spam


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
    FormatDescriptor.nBlockAlign =
        (FormatDescriptor.wBitsPerSample / 8) * FormatDescriptor.nChannels;
    FormatDescriptor.nAvgBytesPerSec =
        FormatDescriptor.nSamplesPerSec * FormatDescriptor.nBlockAlign;
    FormatDescriptor.cbSize = 0;
    return &FormatDescriptor;
}


//-------------------------------------------------------------------------//
//DirectSound and Windows boilerplate


void dll_init(HWND hwnd,int sample_rate) {
    int update_interval = 15;
    
    
    //initialize directsound
        vibe_check(DirectSoundCreate(NULL, &Device, NULL));
        vibe_check(Device -> SetCooperativeLevel(hwnd, DSSCL_PRIORITY));
    
    
    //create the primary buffer
        vibe_check(Device -> CreateSoundBuffer(
            describe_buffer(
                DSBCAPS_PRIMARYBUFFER,
                NULL,
                0
            ),
            &PrimaryBuffer,
            NULL
        ));
        vibe_check(PrimaryBuffer -> Play(0, 0, DSBPLAY_LOOPING));
    
    
    //create the secondary buffer
        buffer_length = (int)((sample_rate / 1000.0) * update_interval * 2.0);
        vibe_check(Device -> CreateSoundBuffer(
            describe_buffer(
                DSBCAPS_GLOBALFOCUS,
                describe_format(sample_rate),
                buffer_length
            ),
            &SecondaryBuffer,
            NULL
        ));
    
    
    //silence,    buffer
        TertiaryBuffer = (unsigned char*)malloc(buffer_length);
        memset(TertiaryBuffer, 128, buffer_length);
        secondary_buffer_lock();
        secondary_buffer_fill();
        vibe_check(SecondaryBuffer -> Play(0, 0, DSBPLAY_LOOPING));
    
    
    //start engine
        pokey_init();
    
    
    //set up callback for refilling the secondary buffer
        SetTimer(NULL, 1, update_interval, (TIMERPROC)timer_callback);
}

int secondary_buffer_lock() {
    //acquire control of secondary buffer
        HRESULT err = SecondaryBuffer -> Lock(
            0,
            buffer_length,
            &lock_chunk1, &lock_size1,
            &lock_chunk2, &lock_size2,
            0
        );
    
    
    //restore and acquire buffer if it's been lost
        if (err == DSERR_BUFFERLOST) {
            SecondaryBuffer -> Restore();
            vibe_check(SecondaryBuffer -> Lock(
                0,
                buffer_length,
                &lock_chunk1, &lock_size1,
                &lock_chunk2, &lock_size2,
                0
            ));
        }
    
    
    //size of required buffer fill
        return lock_size1 + lock_size2;
}

void secondary_buffer_fill() {
    //copy tertiary data to both chunks of secondary buffer
        memcpy(lock_chunk1, TertiaryBuffer, lock_size1);
        if (lock_chunk2 != NULL)
            memcpy(lock_chunk2, TertiaryBuffer + lock_size1, lock_size2);
    
    
    //relinquish control of secondary buffer
        vibe_check(SecondaryBuffer -> Unlock(
            lock_chunk1, lock_size1,
            lock_chunk2, lock_size2
        ));
}

void timer_callback(HWND a, UINT b, UINT_PTR c, DWORD ms) {
    //set up update timer
        pokey_engine(secondary_buffer_lock());
        secondary_buffer_fill();
}


//-------------------------------------------------------------------------//
//Game Maker interface


GMREAL __pokey_dll_init(double hwnd_real,double sample_rate_real) {
    HWND hwnd = (HWND)((int)hwnd_real);
    int sample_rate = (int)sample_rate_real;
    
    dll_init(hwnd,sample_rate);
    
    return 0;
}


//-------------------------------------------------------------------------//
//POKEY Engine


int pok_state;

void pokey_init() {
    //initialize engine variables
        pok_state = 0;
}

void pokey_engine(int amount) {
    //test tone
        int i = 0;
        while (i < amount) {
            pok_state = (pok_state + 1) % 256;
            TertiaryBuffer[i] = 128 + ((pok_state % 32 < 18) ? 5 : -5);
        i += 1;}
}


//-------------------------------------------------------------------------//