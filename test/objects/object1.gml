#define Create_0
/*"/*'/**//* YYD ACTION
lib_id=1
action_id=603
applies_to=self
*/
pokey_init(48000,1)

note=0
type=0
freq=0
#define Mouse_50
/*"/*'/**//* YYD ACTION
lib_id=1
action_id=603
applies_to=self
*/
type=median(0,mouse_x div 100,7)
note=median(12,12+mouse_y div 8,72)
freq=pokey_get_chromatic_frequency(note)

pokey_sound(0,type,freq,0.3,0)
#define Mouse_51
/*"/*'/**//* YYD ACTION
lib_id=1
action_id=603
applies_to=self
*/
type=median(0,mouse_x div 100,7)
note=median(0,mouse_y div 2,254)
freq=pokey_get_byte_frequency(254-note)

pokey_sound(0,type,freq,0.3,0)
#define Mouse_56
/*"/*'/**//* YYD ACTION
lib_id=1
action_id=603
applies_to=self
*/
pokey_stop()
#define Mouse_57
/*"/*'/**//* YYD ACTION
lib_id=1
action_id=603
applies_to=self
*/
pokey_stop()
#define Draw_0
/*"/*'/**//* YYD ACTION
lib_id=1
action_id=603
applies_to=self
*/
draw_text(10,10,
    pokey_get_instrument_name(type)+
    "#note: "+string(note)+
    "#frequency: "+string(freq)+
    "#active: "+string(pokey_get_voices())
)
