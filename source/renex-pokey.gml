#define __pokey_gml_init
    globalvar __pokey_init;
    
    object_event_add(gm82core_object,ev_step,ev_step_end,"__pokey_step()")
    //global.__pokey_last_update=get_timer()


#define __pokey_step
    if (!__pokey_init) {
        __pokey_init=true
        __pokey_dll_init(window_handle())        
    }
    //var __now;__now=get_timer()
    __pokey_update()//(__now-global.__gm82audio_last_update)/1000)
    //global.__pokey_last_update=__now

//
//