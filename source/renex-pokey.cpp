#include "renex-pokey.h"

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
    TertiaryBuffer = (char*)malloc(1024);    
    memset(TertiaryBuffer,128,1024);
    secondary_buffer_fill(TertiaryBuffer);
    
    //play secondary buffer
    vibe_check(SecondaryBuffer->Play(0,0,DSBPLAY_LOOPING));
    
    //set up callback for refilling the secondary buffer
    SetTimer(NULL,1,15,(TIMERPROC)timer_callback);
    
    return 0;
}


//-------------------------------------------------------------------------//


void secondary_buffer_fill(char* data) {
    void* chunk1;
    DWORD size1;
    void* chunk2;
    DWORD size2;
    
    HRESULT err = SecondaryBuffer->Lock(0,1024,&chunk1,&size1,&chunk2,&size2,0);
    if (err == DSERR_BUFFERLOST) {
        SecondaryBuffer->Restore();
        vibe_check(SecondaryBuffer->Lock(0,1024,&chunk1,&size1,&chunk2,&size2,0));
    }
    memcpy(chunk1,data,size1);
    if (chunk2!=NULL) {
        memcpy(chunk2,data+size1,size2);
    }
    
    vibe_check(SecondaryBuffer->Unlock(chunk1,size1,chunk2,size2));
}

void timer_callback(HWND a,UINT b,UINT_PTR c,DWORD ms) {
    //TertiaryBuffer etc.
}