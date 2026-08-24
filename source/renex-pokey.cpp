//---------------------------------------------------------------------------//
/*

    RENEX POKEY
    ===========
    v1.0
    18 Aug 2026
    
  A modern, high quality reimplementation of a POKEY-style sound engine.
    
  Designed for use with Game Maker 8.2.

*/
//---------------------------------------------------------------------------//
/*todo

- fix apparent frequency of the poly instruments using const arr instead of
  macro POKEY_CLOCK_FACTOR

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


//---------------------------------------------------------------------------//
//types


//adjustment factor for the lfsr poly instruments
#define POKEY_CLOCK_FACTOR 1.789790/2.0

#define NUM_CHANNELS 32

struct pokey_settings {
    unsigned char chan_type[NUM_CHANNELS];
    double chan_freq[NUM_CHANNELS];
    float chan_vol[NUM_CHANNELS];
    float chan_pan[NUM_CHANNELS];
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

double pokey_clock_accumulator[NUM_CHANNELS];
int pokey_channel_signal[NUM_CHANNELS];
int pokey_lfsr_reg4[NUM_CHANNELS];
int pokey_lfsr_reg5[NUM_CHANNELS];
int pokey_lfsr_reg9[NUM_CHANNELS];
int pokey_active_channels;

const float pokey_duty_cycle[3] = {
    0.5f,
    18.f/31.f,
    0.125f
};

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

double dll_init(HWND, int, int);
int secondary_buffer_query();
void secondary_buffer_fill(int);
void CALLBACK timer_callback(UINT, UINT, DWORD, DWORD, DWORD);
void copy_settings(volatile pokey_settings*,volatile pokey_settings*);

void pokey_init();
void pokey_set_channel(int, unsigned char, double, float, float);
void pokey_push_settings();
void pokey_timer_callback();
void pokey_generate(int);


//---------------------------------------------------------------------------//
//DirectSound and Windows boilerplate


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

double dll_init(HWND hwnd, int sample_rate, int channels) {
    //initialize directsound
        vibe_check(DirectSoundCreate(NULL, &Device, NULL));
        vibe_check(Device -> SetCooperativeLevel(hwnd, DSSCL_PRIORITY));
    
    
    //initialize some globals
        buffer_sample_rate = sample_rate;
        
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
        
        update_interval = 15; //ms
        buffer_amount = (int)((buffer_sample_rate / 1000.0) * update_interval * 2.0);
        buffer_lastpos = 0;
        pokey_settings_sizeof = sizeof(pokey_settings);
        pokey_active_channels = channels;
    
    
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
    
    
    //get it going
        TertiaryBuffer = (unsigned char*)malloc(buffer_length);
        pokey_timer_callback();
        vibe_check(SecondaryBuffer -> Play(0, 0, DSBPLAY_LOOPING));
    
    return (double)buffer_sample_rate;
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
        
        //* 2 channels
        DWORD write_size = (play_head + buffer_amount * 2) - writewrap;
    
    
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
    pokey_timer_callback();
}

void copy_settings(volatile pokey_settings* from,volatile pokey_settings* to) {
    char* A = (char*)from;
    char* B = (char*)to;
    
    for (int i=0;i<pokey_settings_sizeof;++i) {
        B[i]=A[i];
    }
}


//---------------------------------------------------------------------------//
//Game Maker interface


GMREAL __pokey_dll_init(double hwnd_real, double samplerate_real, double channels_real) {
    return dll_init(
        (HWND)((int)hwnd_real),
        (int)samplerate_real,
        (int)channels_real
    );
}

GMREAL __pokey_dll_update() {
    pokey_push_settings();    
    
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


//---------------------------------------------------------------------------//
//POKEY api


void pokey_init() {
    for (int i=0;i<NUM_CHANNELS;++i) {
        pokey_settings_a.chan_type[i]=0;
        pokey_settings_a.chan_freq[i]=0;
        pokey_settings_a.chan_vol[i]=0;
        pokey_settings_a.chan_pan[i]=0;
        
        pokey_lfsr_reg4[i]=15;
        pokey_lfsr_reg5[i]=31;
        pokey_lfsr_reg9[i]=511;
        
        pokey_clock_accumulator[i]=0;
        pokey_channel_signal[i]=0;
    }
    pokey_settings_a.ready=true;
    copy_settings(&pokey_settings_a,&pokey_settings_b);        
}

void pokey_set_channel(int channel, unsigned char type, double freq, float vol, float pan) {
    pokey_settings_a.chan_type[channel] = type;
    pokey_settings_a.chan_freq[channel] = freq;
    pokey_settings_a.chan_vol[channel] = vol;
    pokey_settings_a.chan_pan[channel] = pan;
}

void pokey_push_settings() {
    //update mailbox if allowed
        if (pokey_settings_b.ready == false)
            copy_settings(&pokey_settings_a,&pokey_settings_b);
}

void pokey_timer_callback() {
    //fill buffer as necessary
        int amount = secondary_buffer_query();
        if (amount) {
            if (pokey_settings_b.ready) {
                copy_settings(&pokey_settings_b,&pokey_settings_c);
                pokey_settings_b.ready=false;
            }
            pokey_generate(amount);
            secondary_buffer_fill(amount);
        }
}


//---------------------------------------------------------------------------//
//linear feedback shift registers


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


//---------------------------------------------------------------------------//
//main synth core


void pokey_generate(int amount) {
    int sample, channel, chancount;
    double mix_left, mix_right;
    
    int chanid[NUM_CHANNELS];
    unsigned char type[NUM_CHANNELS];
    double frequency[NUM_CHANNELS], clkstep[NUM_CHANNELS], period[NUM_CHANNELS];
    double pan_left[NUM_CHANNELS], pan_right[NUM_CHANNELS];
    
    //store active channel settings on the local arrays
    for (channel = 0, chancount = 0; channel < pokey_active_channels; ++channel) {
        frequency[chancount] = pokey_settings_c.chan_freq[channel];
        if (frequency[channel] > 0 && pokey_settings_c.chan_vol[channel] > 0) {
            type[chancount] = pokey_settings_c.chan_type[channel];
            period[chancount] = buffer_sample_rate / frequency[channel];
            pan_left[chancount] = min(1.0,1.0-pokey_settings_c.chan_pan[channel]) * pokey_settings_c.chan_vol[channel];
            pan_right[chancount] = min(1.0,pokey_settings_c.chan_pan[channel]+1.0) * pokey_settings_c.chan_vol[channel];
            chanid[chancount]=channel;
            chancount++;
        }
    }
    
    //main mixer core
    for (sample = 0; sample < amount; sample += 2) {
        mix_left = 0;
        mix_right = 0;
        
        for (int i = 0; i < chancount; ++i) {
            channel=chanid[i];
            
            if (frequency[channel] > 0) {
                
                if (type[channel] < 0x3) {
                    pokey_clock_accumulator[channel] = fmod(pokey_clock_accumulator[channel] + 1, period[channel]);
                    if (pokey_clock_accumulator[channel] / period[channel] < pokey_duty_cycle[type[channel]])
                        pokey_channel_signal[channel] = 1;
                    else 
                        pokey_channel_signal[channel] = 0;
                } else {            
                    ++pokey_clock_accumulator[channel];
                    if (pokey_clock_accumulator[channel] >= period[channel] / POKEY_CLOCK_FACTOR) {
                        pokey_clock_accumulator[channel] = fmod(pokey_clock_accumulator[channel],period[channel] / POKEY_CLOCK_FACTOR);
                        switch (type[channel]) {
                            case 0x3: pokey_channel_signal[channel] = poly4(channel); break;
                            case 0x4: pokey_channel_signal[channel] = poly5(channel); break;
                            case 0x5: pokey_channel_signal[channel] = poly9(channel); break;                        
                            case 0x6: if (poly5(channel)) pokey_channel_signal[channel]=poly4(channel); break;
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
        
        TertiaryBuffer[sample + 0] = (int)(128.0 + 127.0 * mix_left);
        TertiaryBuffer[sample + 1] = (int)(128.0 + 127.0 * mix_right);
    }    
}


//---------------------------------------------------------------------------//