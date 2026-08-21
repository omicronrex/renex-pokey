#include <windows.h>
#include <dsound.h>
#include <stdio.h>

#pragma comment(lib,"dsound.lib")
#pragma comment(lib,"Winmm.lib")

#define GMREAL extern "C" __declspec(dllexport) double __cdecl
#define GMSTR extern "C" __declspec(dllexport) char* __cdecl


//---------------------------------------------------------------------------//
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


//---------------------------------------------------------------------------//
//types


struct pokey_settings {
    unsigned char chan_type[4];
    unsigned char chan_freq[4];
    float chan_vol[4];
    float chan_pan[4];
    char ready;
};


//---------------------------------------------------------------------------//
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

//mailbox system for thread data transfer
int pokey_settings_sizeof;
volatile pokey_settings
    pokey_settings_a,
    pokey_settings_b,
    pokey_settings_c;


//---------------------------------------------------------------------------//
//function prototypes


DSBUFFERDESC* describe_buffer(DWORD, WAVEFORMATEX*, DWORD);
WAVEFORMATEX* describe_format(int);

void dll_init(HWND, int);
int secondary_buffer_query();
void secondary_buffer_fill(int);
void CALLBACK timer_callback(UINT, UINT, DWORD, DWORD, DWORD);
void copy_settings(volatile pokey_settings*,volatile pokey_settings*);

void pokey_init();
void pokey_set_channel(int, unsigned char, unsigned char, float, float);
void pokey_push_settings();
void pokey_update();
void pokey_generate(int);


//---------------------------------------------------------------------------//
//DirectSound and Windows boilerplate


void dll_init(HWND hwnd,int sample_rate) {
    //initialize some globals
        update_interval = 15;    
        buffer_amount = (int)((sample_rate / 1000.0) * update_interval * 2.0);
        buffer_lastpos = 0;
        pokey_settings_sizeof = sizeof(pokey_settings);
    
    
    //initialize directsound
        vibe_check(DirectSoundCreate(NULL, &Device, NULL));
        vibe_check(Device -> SetCooperativeLevel(hwnd, DSSCL_PRIORITY));
    
    
    //create the two dsound buffers
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
        
        //about a 4x safety border (like 8 frames)
        buffer_length = buffer_amount*4;
        
        vibe_check(Device -> CreateSoundBuffer(
            describe_buffer(
                DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2,
                describe_format(sample_rate),
                buffer_length
            ),
            &SecondaryBuffer,
            NULL
        ));
    
    
    //start engine
        pokey_init();
    
    
    //set up callback for refilling the secondary buffer
        timeSetEvent(update_interval,update_interval,timer_callback,0,TIME_PERIODIC);
    
    
    //start buffer
        TertiaryBuffer = (unsigned char*)malloc(buffer_length);
        pokey_update();
        vibe_check(SecondaryBuffer -> Play(0, 0, DSBPLAY_LOOPING));
}

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
    pokey_update();
}

void copy_settings(volatile pokey_settings* struct1,volatile pokey_settings* struct2) {
    char* A = (char*)struct1;
    char* B = (char*)struct2;
    
    for (int i=0;i<pokey_settings_sizeof;++i) {
        B[i]=A[i];
    }
}


//---------------------------------------------------------------------------//
//Game Maker interface


GMREAL __pokey_dll_init(double hwnd_real,double sample_rate_real) {
    dll_init((HWND)((int)hwnd_real),(int)sample_rate_real);
    
    return 0;
}

GMREAL __pokey_dll_update() {
    pokey_push_settings();    
    
    return 0;
}

GMREAL __pokey_sound(double channel,double type,double freq,double vol,double pan) {
    pokey_set_channel(
        (int)channel,
        (unsigned char)type,
        (unsigned char)freq,
        (float)vol,
        (float)pan
    );
    
    return 0;
}


//---------------------------------------------------------------------------//
//POKEY Engine


int pok_state;

void pokey_init() {
    //initialize engine variables
        pok_state = 0;
        
        for (int i=0;i<4;++i) {
            pokey_settings_a.chan_type[i]=0;
            pokey_settings_a.chan_freq[i]=0;
            pokey_settings_a.chan_vol[i]=0;
            pokey_settings_a.chan_pan[i]=0;
        }
        pokey_settings_a.ready=true;
        copy_settings(&pokey_settings_b,&pokey_settings_a);        
}

void pokey_set_channel(int channel, unsigned char type, unsigned char freq, float vol, float pan) {
    pokey_settings_a.chan_type[channel] = type;
    pokey_settings_a.chan_freq[channel] = freq;
    pokey_settings_a.chan_vol[channel] = vol;
    pokey_settings_a.chan_pan[channel] = pan;
}

void pokey_push_settings() {
    //update mailbox if allowed
        if (pokey_settings_b.ready == false)
            copy_settings(&pokey_settings_b,&pokey_settings_a);
}

void pokey_update() {
    //fill buffer as necessary
        int amount = secondary_buffer_query();
        if (amount) {
            if (pokey_settings_b.ready) {
                copy_settings(&pokey_settings_c,&pokey_settings_b);
                pokey_settings_b.ready=false;
            }
            pokey_generate(amount);
            secondary_buffer_fill(amount);
        }
}

void pokey_generate(int amount) {
    //pokey_settings_c etc.
    //test tone
        int i = 0;
        while (i < amount) {
            pok_state = (pok_state + 1) % 1024;
            if (pok_state<512)
                TertiaryBuffer[i] = 128 + ((pok_state % 32 < 18) ? 5 : -5);
            else
                TertiaryBuffer[i] = 128 + ((pok_state % 64 < 36) ? 5 : -5);
        i += 1;}
}


//---------------------------------------------------------------------------//