#define __pokey_gml_init
    globalvar __pokey_version,__pokey_init,__pokey_channels,__pokey_samplerate;
    
    __pokey_version=100
    
    object_event_add(gm82core_object,ev_step,ev_step_end,"__pokey_step()")


#define __pokey_step
    if (__pokey_init) {
        __pokey_dll_update()
    }


#define pokey_init
    ///pokey_init(samplerate,channels)
    //samplerate: audio sample rate (8000 - 48000)
    //channels: number of channels (1 - 32)
    //Pass 0 for the sample rate to use the system sample rate for the
    //highest quality audio.
    //Please note that very low sample rates, while they may sound like a
    //low-pass filter, will also limit the top frequency of sounds played,
    //and cause high frequencies close to the limit to ring.
    
    if (__pokey_init) {
        show_error("Error in function pokey_init: pokey already initialized",false)
        exit
    }
    
    __pokey_channels=median(1,argument1,32)
    
    __pokey_dll_init(
        window_handle(),
        argument0,
        __pokey_channels
    )
    
    __pokey_samplerate=__pokey_get_samplerate()
    
    __pokey_init=true


#define pokey_sound
    ///pokey_sound(channel,type,freq,vol,pan)
    //channel: channel to use (0 - 32)
    //type: instrument id (0 - 6)
    //freq: sound frequency in hz
    //volume: channel volume (0 - 1)
    //pan: channel pan (-1 - 1)
    //Starts playing sound in a channel.
    //To stop a channel, set the volume to 0.
    //Instruments:
    //0: Pulse 1/2 duty
    //1: Pulse 18/31 duty
    //2: Pulse 1/8 duty
    //3: Poly 4
    //4: Poly 5
    //5: Poly 9
    //6: Poly 4 -> Poly 5
    
    if (__pokey_init) {
        __pokey_sound(
            median(0,argument0,__pokey_channels),
            median(0,argument1,6),
            median(0,argument2,__pokey_samplerate),
            sqr(median(0,argument3,1)),
            median(-1,argument4,1),
        )
    }


#define pokey_stop
    ///pokey_stop()
    //Silences all channels.
    
    var __i;__i=0 repeat (__pokey_channels) {__pokey_sound(__i,0,0,0,0) __i+=1}
    
    
#define pokey_get_chromatic_frequency
    ///pokey_get_chromatic_frequency(note)
    //Returns the frequency in Hz of a note in the chromatic scale.
    //The first note is A0 (27.5Hz), with 12 notes in an octave.
    //For example, note 48 (A4, 12x4 from A0) returns 440 Hz.

    //C0 = 27.5Hz
    //12 notes in an octave
    //12th root of 2
    return 27.5*power(2,argument0 div 12)*power(1.05946309436,argument0 mod 12)


#define pokey_get_byte_frequency
    ///pokey_get_byte_frequency(byte)
    //Returns the frequency in Hz of a note byte (0-254) as seen in the
    //original POKEY chip with default settings.
    
    var __note; __note=median(0,floor(argument0),254)+4
    
    //clock speed of NTSC console
    //(clock / (note + 4)) / (note + 4 + 1)
    return (1789790/__note)/(__note+1)
//
//