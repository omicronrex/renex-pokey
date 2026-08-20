#include <windows.h>
#include <dsound.h>
#include <stdio.h>

#pragma comment(lib,"dsound.lib")
#pragma comment(lib,"Winmm.lib")

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

int update_interval;
int buffer_length;
int buffer_amount;
int buffer_lastpos;


//-------------------------------------------------------------------------//
//function prototypes

void dll_init(HWND, int);
void CALLBACK timer_callback(UINT, UINT, DWORD, DWORD, DWORD);

int secondary_buffer_query();
void secondary_buffer_fill(int);

void pokey_init();
void pokey_engine(int);


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
    //initialize some globals
        update_interval = 15;    
        buffer_amount = (int)((sample_rate / 1000.0) * update_interval * 2.0);
        buffer_lastpos = 0;
    
    
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
    
    
    //create the quarter-second secondary buffer
        buffer_length = buffer_amount*2;
        vibe_check(Device -> CreateSoundBuffer(
            describe_buffer(
                DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2,
                describe_format(sample_rate),
                buffer_length
            ),
            &SecondaryBuffer,
            NULL
        ));
    
    
    //silence,    buffer
        TertiaryBuffer = (unsigned char*)malloc(buffer_length);
        memset(TertiaryBuffer, 128, buffer_length);
        secondary_buffer_fill(buffer_length);
        vibe_check(SecondaryBuffer -> Play(0, 0, DSBPLAY_LOOPING));
    
    
    //start engine
        pokey_init();
    
    
    //set up callback for refilling the secondary buffer
        timeSetEvent(update_interval,update_interval,timer_callback,0,TIME_PERIODIC);
}

int secondary_buffer_query() {
    //restore a lost buffer
        DWORD status;
        SecondaryBuffer -> GetStatus(&status);
        if (status == DSERR_BUFFERLOST) {
            SecondaryBuffer -> Restore();
        }
    
    
    //get head position, calculate size to write
        DWORD play_head;
        
        vibe_check(SecondaryBuffer -> GetCurrentPosition(&play_head,NULL));
        
        DWORD writewrap;
        
        if (buffer_lastpos<play_head) {
            //wrapped
            writewrap = buffer_lastpos + buffer_length;
        } else {
            writewrap = buffer_lastpos;
        }        
        
        DWORD write_size = (play_head + buffer_amount) - writewrap;
    
    
    //if we're running too fast, nop out
        if (write_size<=0) {            
            return 0;
        }
    
    
    //size of required buffer fill
        return write_size;
}

void secondary_buffer_fill(int amount) {
    void* lock_chunk1;
    DWORD lock_size1;
    void* lock_chunk2;
    DWORD lock_size2;

    //acquire control of secondary buffer
        vibe_check(SecondaryBuffer -> Lock(
            buffer_lastpos,
            amount,
            &lock_chunk1, &lock_size1,
            &lock_chunk2, &lock_size2,
            0
        ));
        
        buffer_lastpos = (buffer_lastpos + amount) % buffer_length;
    
    
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

void CALLBACK timer_callback(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dw1, DWORD dw2) {
    //fill buffer as necessary
        int amount = secondary_buffer_query();
        if (amount) {
            pokey_engine(amount);
            secondary_buffer_fill(amount);
        }
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