#define __pokey_gml_init
    globalvar __pokey_init;
    
    object_event_add(gm82core_object,ev_step,ev_step_end,"__pokey_step()")


#define __pokey_step
    if (!__pokey_init) {
        __pokey_init=true
        __pokey_dll_init(window_handle(),22050)        
    }
    __pokey_dll_update()

//
//