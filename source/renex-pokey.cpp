//---------------------------------------------------------------------------//
/*

    RENEX POKEY
    ===========
    18 Aug 2026
    
    A modern, high quality reimplementation of a POKEY-style sound engine.
    
    Designed for use with Game Maker 8.2.

*/
//---------------------------------------------------------------------------//


#include <windows.h>
#include <dsound.h>
#include <stdio.h>

#pragma comment(lib,"dsound.lib")
#pragma comment(lib,"Winmm.lib")

#define GMREAL extern "C" __declspec(dllexport) double __cdecl
#define GMSTR extern "C" __declspec(dllexport) char* __cdecl


//---------------------------------------------------------------------------//
/*todo

- wrap all dll functions in gml helpers that also check ranges and etc.
- abstract channel count into an init setting
- have a separate entrypoint that initializes memory so you can use setup
  functions before directsound is initialized with the hwnd
- double buffer width, implement stereo sound, implement panning

*/
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
int buffer_sample_rate;


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
        buffer_sample_rate = sample_rate;
        update_interval = 15;
        buffer_amount = (int)((buffer_sample_rate / 1000.0) * update_interval * 2.0);
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
                describe_format(buffer_sample_rate),
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


GMREAL __pokey_dll_init(double hwnd_real, double sample_rate_real) {
    dll_init(
        (HWND)((int)hwnd_real),
        (int)sample_rate_real
    );
    
    return 0;
}

GMREAL __pokey_dll_update() {
    pokey_push_settings();    
    
    return 0;
}

GMREAL __pokey_sound(double channel, double type, double freq, double vol, double pan) {
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


double pokey_clock_accumulator[4];
int pokey_clock_counter[4];
int pokey_channel_signal[4];
int pokey_lfsr_reg4[4];
int pokey_lfsr_reg5[4];
int pokey_lfsr_reg9[4];


void pokey_init() {
    for (int i=0;i<4;++i) {
        pokey_settings_a.chan_type[i]=0;
        pokey_settings_a.chan_freq[i]=0;
        pokey_settings_a.chan_vol[i]=0;
        pokey_settings_a.chan_pan[i]=0;
        
        pokey_lfsr_reg4[i]=15;
        pokey_lfsr_reg5[i]=31;
        pokey_lfsr_reg9[i]=511;
        
        pokey_clock_accumulator[i]=0;
        pokey_clock_counter[i]=0;
        pokey_channel_signal[i]=0;
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


//-------------------------------------------------------------------------//
//clock helpers


double getnotefreq(int note) {
    if (note == 0) return 0;
    
    double freq = 55.0;
    
    for (int i = 0; i < note; i += 12) {
        freq *= 2.0;
    }
    for (int i = 0; i < note % 12; ++i) {
        freq *= 1.05946309436;
    }
    
    return 55.0 * freq;
}

int poly4(unsigned char channel) {
    int r = pokey_lfsr_reg4[channel]>>1;
    r = (r&~8)|((r&1)^((r&2)>>1))<<3;
    pokey_lfsr_reg4[channel] = r;

    return r&1;
}

int poly5(unsigned char channel) {
    int r = pokey_lfsr_reg5[channel]>>1;
    r = (r&~16)|((r&1)^((r&2)>>1))<<4;
    pokey_lfsr_reg5[channel] = r;

    return r&1;
}

int poly9(unsigned char channel) {
    int r = pokey_lfsr_reg9[channel]>>1;
    r = (r&~256)|((r&1)^((r&8)>>3))<<8;
    pokey_lfsr_reg9[channel] = r;

    return r&1;
}

int clock(unsigned char channel,int factor) {
    return (pokey_clock_counter[channel] % factor < 1)?1:0;
}

int square(unsigned char channel,int length,float duty) {
    return (pokey_clock_counter[channel]%length < length*duty)?1:0;
}


//-------------------------------------------------------------------------//
//main synth core


void pokey_generate(int amount) {
    int sample,channel;
    double frequency[4], clkstep[4], period;
    float mix;
    
    for (channel = 0; channel < 4; ++channel) {
        frequency[channel] = getnotefreq(pokey_settings_c.chan_freq[channel]);
        period = buffer_sample_rate / frequency[channel];
        clkstep[channel] = period / 1.79;
    }
    
    for (sample = 0; sample < amount; ++sample) {
        mix = 0;
        for (channel = 0; channel < 4; ++channel) {
            pokey_clock_accumulator[channel]++;
            if (pokey_clock_accumulator[channel] > clkstep[channel]) {
                pokey_clock_counter[channel] = (++pokey_clock_counter[channel]) % 1116;
                pokey_clock_accumulator[channel] -= clkstep[channel];
                
                if (frequency[channel] > 0) switch (pokey_settings_c.chan_type[channel]) {
                    case 0x1: pokey_channel_signal[channel]=poly4(channel); break;
                    case 0x2: if (clock(channel,18)) pokey_channel_signal[channel]=poly4(channel); break;
                    case 0x3: if (poly5(channel)) pokey_channel_signal[channel]=poly4(channel); break;
                    
                    case 0x4: 
                    case 0x5: pokey_channel_signal[channel]=!pokey_channel_signal[channel]; break;
                    
                    case 0x6: 
                    case 0xa: pokey_channel_signal[channel]=square(channel,31,18/31); break;
                    
                    case 0x7:
                    case 0x9: pokey_channel_signal[channel]=poly5(channel); break;
                    
                    case 0x8: pokey_channel_signal[channel]=poly9(channel); break;
                    
                    case 0xc:
                    case 0xd: if (clock(channel,3)) pokey_channel_signal[channel]=!pokey_channel_signal[channel]; break;
                    
                    case 0xe: pokey_channel_signal[channel]=square(channel,93,18/31); break;
                    
                    case 0xf: if (clock(channel,3)) pokey_channel_signal[channel]=poly5(channel); break;
                    
                    default: pokey_channel_signal[channel]=0; break;
                }
            }
            
            mix += pokey_channel_signal[channel] * pokey_settings_c.chan_vol[channel];
        }
        TertiaryBuffer[sample] = (int)(128.0 + 127.0 * mix / 4.0);
    }    
}

//---------------------------------------------------------------------------//