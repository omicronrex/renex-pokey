#define Create_0
/*"/*'/**//* YYD ACTION
lib_id=1
action_id=603
applies_to=self
*/
pokey_init(48000,1)

note=0
type=0
#define Mouse_50
/*"/*'/**//* YYD ACTION
lib_id=1
action_id=603
applies_to=self
*/
type=median(0,mouse_x div 100,6)
note=median(12,12+mouse_y div 8,72)
pokey_sound(0,type,pokey_get_chromatic_frequency(note),0.3,0)
#define Mouse_56
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
    "#active: "+string(pokey_get_voices())
)
