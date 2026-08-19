#include "renex-pokey.h"


//-------------------------------------------------------------------------//


int secondary_buffer_lock() {
    //acquire control of secondary buffer
        HRESULT err = SecondaryBuffer -> Lock(
            0,
            buffer_length,
            &lock_chunk1, &lock_size1,
            &lock_chunk2, &lock_size2,
            0
        );
    
    
    //restore and acquire secondary buffer if it's been lost        
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


GMREAL __pokey_dll_init(double hwnd_real) {
    //defaults
        int update_interval = 15;
        int sample_rate = 22050;
    
    
    //initialize directsound
        HWND hwnd = (HWND)((int)hwnd_real);
        vibe_check(DirectSoundCreate(NULL, &Device, NULL));
        vibe_check(Device -> SetCooperativeLevel(hwnd, DSSCL_PRIORITY));    
    
    
    //create and start the primary buffer
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
    
    
    //create the secondary buffer to hold our audio frame
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
    
    
    //fill secondary buffer with test data
        TertiaryBuffer = (unsigned char*)malloc(buffer_length);    
        memset(TertiaryBuffer, 128, buffer_length);
        secondary_buffer_lock();
        secondary_buffer_fill();
    
    
    //play secondary buffer
        vibe_check(SecondaryBuffer -> Play(0, 0, DSBPLAY_LOOPING));
    
    
    //start pokey engine
        pokey_startup();
    
    
    //set up callback for refilling the secondary buffer
        SetTimer(NULL, 1, update_interval, (TIMERPROC)timer_callback);
    
    
    return 0;
}


//-------------------------------------------------------------------------//


//internal psg variables
    int pok_state;
    
    
//-------------------------------------------------------------------------//


void pokey_startup() {
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