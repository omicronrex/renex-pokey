//---------------------------------------------------------------------------//
/*

    RENEX POKEY
    ===========
    18 Aug 2026
  
  A modern, high quality reimplementation of a POKEY-style sound engine.
  
  Designed for use with Game Maker 8.2.

*/
//---------------------------------------------------------------------------//
/*todo

- 

*/
//---------------------------------------------------------------------------//
//header

#include <windows.h>
#include <dsound.h>
#include <stdio.h>
#include <math.h>

#pragma comment(lib,"dsound.lib")
#pragma comment(lib,"Winmm.lib")

#define GMREAL extern "C" __declspec(dllexport) double __cdecl
#define GMSTR extern "C" __declspec(dllexport) char* __cdecl


//---------------------------------------------------------------------------//
//debug helpers 🖐


#define WIDE2(x) L##x
#define WIDE1(x) WIDE2(x)

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

#define vibe_check(a) __vibe_check(WIDE1(__FILE__),__LINE__,a)

extern void debug_message(const wchar_t* msg) {    
    MessageBoxW(0, msg, L"Debug message", 0);
}

extern void debug_message(const wchar_t* msg, int value) {    
    wchar_t buf[1024];
    _snwprintf_s(buf, 1024, msg, value);
    MessageBoxW(0, buf, L"Debug message", 0);
}


//---------------------------------------------------------------------------//
//types and globals


//arbitrary limit
#define NUM_CHANNELS 32

struct pokey_settings {
    unsigned char chan_type[NUM_CHANNELS];
    double chan_freq[NUM_CHANNELS];
    float chan_vol[NUM_CHANNELS];
    float chan_pan[NUM_CHANNELS];
};

MMRESULT current_timer;
LPDIRECTSOUND Device;
LPDIRECTSOUNDBUFFER PrimaryBuffer;
LPDIRECTSOUNDBUFFER SecondaryBuffer;
DSBUFFERDESC BufferDescriptor;
WAVEFORMATEX FormatDescriptor;
unsigned char* TertiaryBuffer;
unsigned char* QuadBuffer;

//square wave instrument duty cycle
const float pokey_duty_cycle[3] = {
    0.5f,
    18.f/31.f,
    0.125f
};

//frequency correction factors for instruments
const double pokey_tuning[9] = {
    1.0,
    1.0,
    1.0,
    4.0 / 1.05946309436,
    4.0,
    4.0,
    4.0 / 1.05946309436,
    4.0,
    4.0
};

int update_interval;
int buffer_length;
int buffer_amount;
int buffer_lastpos;
int buffer_sample_rate;

int tert_length;
int tert_readpos;
int tert_writepos;


//pokey internal state
    double pokey_clock_accumulator[NUM_CHANNELS];
    int pokey_channel_signal[NUM_CHANNELS];
    int pokey_lfsr_reg4[NUM_CHANNELS];
    int pokey_lfsr_reg5[NUM_CHANNELS];
    int pokey_lfsr_reg9[NUM_CHANNELS];
    int pokey_lfsr_reg17[NUM_CHANNELS];
    int pokey_active_channels;


//mailbox system for thread data transfer
    int pokey_settings_sizeof;
    volatile pokey_settings
        pokey_settings_a,
        pokey_settings_b;


//---------------------------------------------------------------------------//
//function prototypes


void dll_init(HWND, int, int);
int secondary_buffer_query();
void secondary_buffer_fill(int);
void CALLBACK timer_callback(UINT, UINT, DWORD, DWORD, DWORD);
void copy_settings(volatile pokey_settings*,volatile pokey_settings*);

void pokey_init();
void pokey_set_channel(int, unsigned char, double, float, float);
void pokey_frame_update(double);
int pokey_get_voices();
void pokey_timer_callback();
void pokey_generate(int);


//---------------------------------------------------------------------------//
//DirectSound and DLL boilerplate


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
    FormatDescriptor.nChannels = 2;
    FormatDescriptor.nSamplesPerSec = (DWORD)sample_rate;
    FormatDescriptor.wBitsPerSample = 8;
    FormatDescriptor.nBlockAlign =
        (FormatDescriptor.wBitsPerSample / 8) * FormatDescriptor.nChannels;
    FormatDescriptor.nAvgBytesPerSec =
        FormatDescriptor.nSamplesPerSec * FormatDescriptor.nBlockAlign;
    FormatDescriptor.cbSize = 0;
    return &FormatDescriptor;
}

void dll_init(HWND hwnd, int sample_rate, int channels) {
    buffer_sample_rate = sample_rate;
    pokey_active_channels = channels;
    
    
    //initialize directsound
        vibe_check(DirectSoundCreate(NULL, &Device, NULL));
        vibe_check(Device -> SetCooperativeLevel(hwnd, DSSCL_PRIORITY));
    
    
    //initialize some globals
        update_interval = 15; //ms
        buffer_lastpos = 0;
        
        pokey_settings_sizeof = sizeof(pokey_settings);    
        
        DSCAPS dscaps; 
        dscaps.dwSize = sizeof(DSCAPS);
        vibe_check(Device -> GetCaps(&dscaps));
        
        if (buffer_sample_rate <= 0) {
            buffer_sample_rate = dscaps.dwMaxSecondarySampleRate;
        } else if (buffer_sample_rate < dscaps.dwMinSecondarySampleRate) {
            buffer_sample_rate = dscaps.dwMinSecondarySampleRate;
        } else if (buffer_sample_rate > dscaps.dwMaxSecondarySampleRate) {
            buffer_sample_rate = dscaps.dwMaxSecondarySampleRate;
        }
        
        buffer_amount = (int)(buffer_sample_rate/10);
    
    
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
        
        //a good amount of dsound buffering
        buffer_length = buffer_amount * 4;
        
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
    
    
    //set up tertiary circular buffer with a decent amount of space
    //effectively, until dsound's 10ms update rate jitter is negligible
        tert_length = (buffer_sample_rate / 10) * 2;
        TertiaryBuffer = (unsigned char*)malloc(tert_length);
        tert_readpos = 0;
        tert_writepos = 0;
    
    
    //set up 4th buffer for copy into the secondary
        QuadBuffer = (unsigned char*)malloc(tert_length);
    
    
    //get it going
        pokey_timer_callback();
        vibe_check(SecondaryBuffer -> Play(0, 0, DSBPLAY_LOOPING));
        
        
    //set up callback for refilling the secondary buffer
        current_timer = timeSetEvent(
            update_interval,
            update_interval,
            timer_callback,
            0,
            TIME_PERIODIC
        );    
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
        
        vibe_check(SecondaryBuffer -> GetCurrentPosition(&play_head, NULL));
        
        DWORD writewrap;
        
        if (buffer_lastpos<play_head) {
            //wrapped
            writewrap = buffer_lastpos + buffer_length;
        } else {
            writewrap = buffer_lastpos;
        }        
        
        //* 2 channels
        DWORD write_size = (play_head + buffer_amount * 2) - writewrap;
    
    
    //if we're running too fast, nop out
        if (write_size <= 0) {
            return 0;
        }        
    
    
    //if we're running too slow, uhhh
        if (write_size>buffer_amount * 2) write_size = buffer_amount * 2;
    
    
    //size of required buffer fill
        return write_size;
}

void secondary_buffer_fill(int amount) {
    void* lock_chunk1;
    DWORD lock_size1;
    void* lock_chunk2;
    DWORD lock_size2;
    
    
    //copy tertiary buffer to temp quad buffer
        int start = tert_readpos;
        int end = start + amount;
        if (end > tert_length) {
            //copy both regions to quad
            int crop = tert_length - start;
            memcpy(QuadBuffer,TertiaryBuffer+start,crop);
            memcpy(QuadBuffer+crop,TertiaryBuffer,amount-crop);
        } else {
            memcpy(QuadBuffer,TertiaryBuffer+start,amount);
        }
        tert_readpos += amount;
        if (tert_readpos >= tert_length) tert_readpos -= tert_length;
    
    
    //acquire control of secondary buffer
        vibe_check(SecondaryBuffer -> Lock(
            buffer_lastpos,
            amount,
            &lock_chunk1, &lock_size1,
            &lock_chunk2, &lock_size2,
            0
        ));
        
        buffer_lastpos = (buffer_lastpos + amount) % buffer_length;
    
    
    //copy quad data to both chunks of secondary buffer
        memcpy(lock_chunk1, QuadBuffer, lock_size1);
        if (lock_chunk2 != NULL)
            memcpy(lock_chunk2, QuadBuffer + lock_size1, lock_size2);
    
    
    //relinquish control of secondary buffer
        vibe_check(SecondaryBuffer -> Unlock(
            lock_chunk1, lock_size1,
            lock_chunk2, lock_size2
        ));
}

void CALLBACK timer_callback(UINT, UINT, DWORD, DWORD, DWORD) {
    pokey_timer_callback();
}

void copy_settings(volatile pokey_settings* from,volatile pokey_settings* to) {
    char* A = (char*)from;
    char* B = (char*)to;
    
    for (int i = 0; i < pokey_settings_sizeof; ++i) {
        B[i] = A[i];
    }
}


//---------------------------------------------------------------------------//
//Game Maker interface


GMREAL __pokey_dll_init(double hwnd_real, double samplerate_real, double channels_real) {
    dll_init(
        (HWND)(int)hwnd_real,
        (int)samplerate_real,
        (int)channels_real
    );
    
    return 0;
}

GMREAL __pokey_dll_update(double gen_real) {    
    pokey_frame_update(gen_real);
    
    return 0;
}

GMREAL __pokey_sound(double channel, double type, double freq, double vol, double pan) {
    pokey_set_channel(
        (int)channel,
        (unsigned char)type,
        freq,
        (float)vol,
        (float)pan
    );
    
    return 0;
}

GMREAL __pokey_get_voices() {
    ///pokey_get_voices()
    //Returns the current number of active channels.
    //A channel is considered active when frequency and volume are not zero.
    
    return pokey_get_voices();
}

GMREAL __pokey_get_tuning(double type) {
    return pokey_tuning[(int)type];
}


//---------------------------------------------------------------------------//
//POKEY api


void pokey_init() {
    for (int i = 0; i < NUM_CHANNELS; ++i) {
        pokey_settings_a.chan_type[i] = 0;
        pokey_settings_a.chan_freq[i] = 0;
        pokey_settings_a.chan_vol[i] = 0;
        pokey_settings_a.chan_pan[i] = 0;
        
        pokey_lfsr_reg4[i] = 15;
        pokey_lfsr_reg5[i] = 31;
        pokey_lfsr_reg9[i] = 511;
        pokey_lfsr_reg17[i] = 131071;
        
        pokey_clock_accumulator[i] = 0;
        pokey_channel_signal[i] = 0;
    }
    copy_settings(&pokey_settings_a, &pokey_settings_b);
}

int get_tertiary_health() {
    int health;
    
    if (tert_writepos < tert_readpos)
        health = tert_writepos + tert_length - tert_readpos;
    else
        health = tert_writepos - tert_readpos;
    
    return health;
}

void pokey_set_channel(int channel, unsigned char type, double freq, float vol, float pan) {
    pokey_settings_a.chan_type[channel] = type;
    pokey_settings_a.chan_freq[channel] = freq;
    pokey_settings_a.chan_vol[channel] = vol*vol;
    pokey_settings_a.chan_pan[channel] = pan;
}

void pokey_frame_update(double amount_ms) {
    copy_settings(&pokey_settings_a, &pokey_settings_b);
    
    int amount = (int)(amount_ms * (buffer_sample_rate / 1000.0)) * 2;
    
    int health = get_tertiary_health();
    
    int margin = (int)(tert_length * 0.9);    
    
    if (health + amount > margin) amount = margin - health;
    
    pokey_generate(amount);
}

int pokey_get_voices() {
    int channel, chancount;
    
    for (channel = 0, chancount = 0; channel < pokey_active_channels; ++channel) {
        if (pokey_settings_b.chan_freq[channel] > 0 && pokey_settings_b.chan_vol[channel] > 0) {
            ++chancount;
        }
    }
    
    return (int)chancount;
}

void pokey_timer_callback() {
    //fill buffer as necessary
    
    int amount = secondary_buffer_query();
    
    if (amount) {
        int health = get_tertiary_health();
        
        if (health < amount) {        
            pokey_generate(amount - health);
        }
        
        secondary_buffer_fill(amount);
    }
}


//---------------------------------------------------------------------------//
//linear feedback shift register iterators


int poly4(unsigned char channel) {
    int r = pokey_lfsr_reg4[channel];
    r = ((((r + r)) + (((r >> 2) ^ (r >> 3)) & 1))) & 0xf;
    pokey_lfsr_reg4[channel] = r;
    return r&1;
}

int poly5(unsigned char channel) {
    int r = pokey_lfsr_reg5[channel];
    r = (((r + r)) + (((r >> 2) ^ (r >> 4)) & 1)) & 0x1f;
    pokey_lfsr_reg5[channel] = r;
    return r&1;
}

int poly9(unsigned char channel) {
    int r = pokey_lfsr_reg9[channel];
    r = ((r >> 1)) + (((r << 8) ^ (r << 3)) & 0x100);
    pokey_lfsr_reg9[channel] = r;
    return r&1;
}

int poly17(unsigned char channel) {
    int r = pokey_lfsr_reg17[channel];
    r = ((r >> 1)) + (((r << 16) ^ (r << 11)) & 0x10000);
    pokey_lfsr_reg17[channel] = r;
    return r&1;
}


//---------------------------------------------------------------------------//
//main synth core


void pokey_generate(int amount) {
    int sample, channel, chancount;
    double mix_left, mix_right;
    
    int chanid[NUM_CHANNELS];
    unsigned char type[NUM_CHANNELS];
    double frequency[NUM_CHANNELS], clkstep[NUM_CHANNELS], period[NUM_CHANNELS];
    double pan_left[NUM_CHANNELS], pan_right[NUM_CHANNELS];
    
    for (channel = 0, chancount = 0; channel < pokey_active_channels; ++channel) {
        type[chancount] = pokey_settings_b.chan_type[channel];
        frequency[chancount] = pokey_settings_b.chan_freq[channel] * pokey_tuning[type[chancount]];
        if (frequency[channel] > 0 && pokey_settings_b.chan_vol[channel] > 0) {
            period[chancount] = buffer_sample_rate / frequency[channel];
            pan_left[chancount] = min(1.0, 1.0 - pokey_settings_b.chan_pan[channel]) * pokey_settings_b.chan_vol[channel];
            pan_right[chancount] = min(1.0, pokey_settings_b.chan_pan[channel] + 1.0) * pokey_settings_b.chan_vol[channel];
            chanid[chancount] = channel;
            chancount++;
        }
    }
    
    
    //main mixer core    
    int offset = tert_writepos;
    int addr;
    
    for (sample = 0; sample < amount; sample += 2) {
        mix_left = 0;
        mix_right = 0;
        
        for (int i = 0; i < chancount; ++i) {
            channel=chanid[i];            
            if (frequency[channel] > 0) {                
                if (type[channel] < 0x3) {
                    //pulse types - per-sample duty cycle, and latch-off for period change protection
                    ++pokey_clock_accumulator[channel];
                    if (pokey_clock_accumulator[channel] >= period[channel]) {
                        pokey_clock_accumulator[channel] = fmod(pokey_clock_accumulator[channel],period[channel]);
                        pokey_channel_signal[channel] = 1;
                    } else if (pokey_channel_signal[channel] && pokey_clock_accumulator[channel] / period[channel] >= pokey_duty_cycle[type[channel]]) {
                        pokey_channel_signal[channel] = 0;
                    }
                } else {
                    //poly types - iterate at period crossings
                    ++pokey_clock_accumulator[channel];
                    if (pokey_clock_accumulator[channel] >= period[channel]) {
                        pokey_clock_accumulator[channel] = fmod(pokey_clock_accumulator[channel],period[channel]);
                        switch (type[channel]) {
                            case 0x3: pokey_channel_signal[channel] = poly4(channel); break;
                            case 0x4: pokey_channel_signal[channel] = poly5(channel); break;
                            case 0x5: pokey_channel_signal[channel] = poly9(channel); break;
                            case 0x6: if (poly5(channel)) pokey_channel_signal[channel] = poly4(channel); break;
                            case 0x7: pokey_channel_signal[channel] = poly17(channel); break;
                            case 0x8: if (poly9(channel)) pokey_channel_signal[channel] = poly17(channel); break;
                        }
                    }
                }
            } else {
                pokey_channel_signal[channel] = 0;
            }
            
            mix_left += pokey_channel_signal[channel] * pan_left[channel];
            mix_right += pokey_channel_signal[channel] * pan_right[channel];
        }
        
        if (chancount > 0) {
            mix_left /= chancount;
            mix_right /= chancount;
        }
        
        addr = offset + sample;
        if (addr >= tert_length) addr -= tert_length;
        
        TertiaryBuffer[addr + 0] = (int)(128.0 + 127.0 * mix_left);
        TertiaryBuffer[addr + 1] = (int)(128.0 + 127.0 * mix_right);
    }
    
    tert_writepos += amount;
    if (tert_writepos >= tert_length) tert_writepos -= tert_length;
}


//---------------------------------------------------------------------------//