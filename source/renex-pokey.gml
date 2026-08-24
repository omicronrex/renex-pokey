#define __pokey_gml_init
    globalvar
        __pokey_version,
        __pokey_init,
        __pokey_channels,
        __pokey_samplerate;
    
    __pokey_version=100
    
    object_event_add(gm82core_object,ev_step,ev_step_end,"if (__pokey_init) __pokey_dll_update()")


#define pokey_init
    ///pokey_init(samplerate,channels)
    //samplerate: audio sample rate (8000-48000)
    //channels: number of channels (1-32)
    //Starts the sound engine with the desired settings.
    //The sample rate is limited by the system's capabilities.
    //Usually, values between 8000 and 48000 are acceptable.
    //You can pass 0 or 'noone' to use the system's default sample rate, for the clearest audio possible.
    //The number of channels is the number of unique sound voices to use, limited between 1 and 32.
    //Each channel can play a single note at a time.
    
    if (__pokey_init) {
        show_error("Error in function pokey_init: pokey is already initialized.",false)
        exit
    }
    
    __pokey_channels=median(1,argument1,32)
    
    __pokey_samplerate=__pokey_dll_init(
        window_handle(),
        argument0,
        __pokey_channels
    )
    
    __pokey_init=true


#define pokey_update()
    ///pokey_update()
    //Updates the pokey engine with the latest instrument data.
    //Note that is is normally done automatically; you only need to call it manually if you're f.ex. in a deadlock for a loading screen or a room transition, etc.
    
    if (__pokey_init) __pokey_dll_update()


#define pokey_sound
    ///pokey_sound(channel,type,freq,vol,pan)
    //channel: channel to use (0 - 31)
    //type: instrument id (0 - 6)
    //freq: sound frequency in hz
    //volume: channel volume (0 - 1)
    //pan: channel pan (-1 - 1)
    //Starts playing sound in a channel.
    //To stop a channel, set the volume to 0.
    //Do note that the maximum frequency that can be played is below half of your sample rate, anything higher than that will have "beating" artifacts.
    //The available instrument types are identifiable with the pk_ constants.
    //More instrument information is available in the definition for pokey_get_instrument_name.
    
    if (__pokey_init) {
        __pokey_sound(
            median(0,argument0,__pokey_channels-1),
            median(0,argument1,6),
            median(0,argument2,__pokey_samplerate),
            sqr(median(0,argument3,1)),
            median(-1,argument4,1),
        )
    }


#define pokey_stop
    ///pokey_stop()
    //Silences all channels.
    //Calling this function is equivalent to setting the volume to 0 on all channels.
    var __i;
    
    if (__pokey_init) {
        __i=0 repeat (__pokey_channels) {__pokey_sound(__i,0,0,0,0) __i+=1}
    }
    
    
#define pokey_get_chromatic_frequency
    ///pokey_get_chromatic_frequency(note)
    //note: note to convert to hertz
    //Returns the frequency in Hz of a note on the chromatic scale.
    //The first note is A0 (27.5Hz), with 12 notes in an octave.
    //For example, note 48 (A4, 4 octaves above A0) returns 440 Hz.
    
    //27.5 = C0 in hz
    //12 = notes in an octave
    //1.06... = 12th root of 2
    return 27.5*power(2,argument0 div 12)*power(1.05946309436,argument0 mod 12)


#define pokey_get_byte_frequency
    ///pokey_get_byte_frequency(byte)
    //byte: note id (0-254)
    //Returns the frequency in Hz of a note byte from 0 to 254 as heard in the original POKEY chip with default settings.
    //If you're going for a retro aesthetic, using only frequencies returned by this function should provide a more authentic sound flavor.
    var __note;
    
    __note=median(0,floor(argument0),254)+4
    
    //clock speed of NTSC console
    //(clock / (note + 4)) / (note + 4 + 1)
    return (1789790/__note)/(__note+1)


#define pokey_get_instrument_name
    ///pokey_get_instrument_name(type)
    //Returns a string with the internal name of the selected instrument.
    //Available instrument types:
    //pk_pulse (0): Pulse 1/2 duty
    //pk_p1813 (1): Pulse 18/31 duty
    //pk_p125 (2): Pulse 1/8 duty
    //pk_poly4 (3): Poly 4
    //pk_poly5 (4): Poly 5
    //pk_poly9 (5): Poly 9
    //pk_poly4_5 (6): Poly 4 -> Poly 5
    
    return pick(
        median(0,argument0,6),
        "Pulse 1/2 duty",
        "Pulse 18/31 duty",
        "Pulse 1/8 duty",
        "Poly 4",
        "Poly 5",
        "Poly 9",
        "Poly 4 -> Poly 5"
    )
//
//