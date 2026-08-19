#include "renex-pokey.h"

static int pok_state;

GMREAL __pokey_dll_init(double hwnd_real) {
    //initialize directsound
    HWND hwnd = (HWND)((int)hwnd_real);
    vibe_check(DirectSoundCreate(NULL,&Device,NULL));
    vibe_check(Device->SetCooperativeLevel(hwnd,DSSCL_PRIORITY));    
    
    //create and start the primary buffer
    vibe_check(Device->CreateSoundBuffer(
        describe_buffer(
            DSBCAPS_PRIMARYBUFFER,
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
            DSBCAPS_GLOBALFOCUS,
            describe_format(),
            1024
        ),
        &SecondaryBuffer,
        NULL
    ));
    
    //fill secondary buffer with test data
    TertiaryBuffer = (unsigned char*)malloc(1024);    
    memset(TertiaryBuffer,128,1024);
    secondary_buffer_lock();
    secondary_buffer_fill();
    
    //play secondary buffer
    vibe_check(SecondaryBuffer->Play(0,0,DSBPLAY_LOOPING));
    
    pok_state = 0;
    
    //set up callback for refilling the secondary buffer
    SetTimer(NULL,1,15,(TIMERPROC)timer_callback);
    
    return 0;
}


//-------------------------------------------------------------------------//


int secondary_buffer_lock() {
    HRESULT err = SecondaryBuffer->Lock(0,1024,&lock_chunk1,&lock_size1,&lock_chunk2,&lock_size2,0);
    if (err == DSERR_BUFFERLOST) {
        SecondaryBuffer->Restore();
        vibe_check(SecondaryBuffer->Lock(0,1024,&lock_chunk1,&lock_size1,&lock_chunk2,&lock_size2,0));
    }
    
    return lock_size1+lock_size2;
}

void secondary_buffer_fill() {
    memcpy(lock_chunk1,TertiaryBuffer,lock_size1);
    if (lock_chunk2!=NULL) {
        memcpy(lock_chunk2,TertiaryBuffer+lock_size1,lock_size2);
    }
    
    vibe_check(SecondaryBuffer->Unlock(lock_chunk1,lock_size1,lock_chunk2,lock_size2));
}

void timer_callback(HWND a,UINT b,UINT_PTR c,DWORD ms) {
    int amount = secondary_buffer_lock();    
    
    int i=0;
    while (i<amount) {
        pok_state=(pok_state+1)%256;
        if (pok_state%16<6)
            TertiaryBuffer[i]=144;
        else
            TertiaryBuffer[i]=112;
    i+=1;}
    
    secondary_buffer_fill();
}