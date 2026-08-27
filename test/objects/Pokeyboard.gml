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
mode=0
#define Mouse_50
/*"/*'/**//* YYD ACTION
lib_id=1
action_id=603
applies_to=self
*/
i=0 repeat (9) {
    u=100+125*i
    if (point_in_rectangle(mouse_x,mouse_y,u,50,u+100,100)) type=i
i+=1}

if (point_in_rectangle(mouse_x,mouse_y,50,300,50*60*20,400)) {
    note=median(12,12+(mouse_x-50) div 20,72)
    freq=pokey_get_chromatic_frequency(note)
    mode=0
}

if (point_in_rectangle(mouse_x,mouse_y,50,150,50*60*20,250)) {
    note=median(0,floor((mouse_x-50)/(60*20)*254),254)
    freq=pokey_get_byte_frequency(254-note)
    mode=1
}

pokey_sound(0,type,freq,0.3,0)
#define Mouse_56
/*"/*'/**//* YYD ACTION
lib_id=1
action_id=603
applies_to=self
*/
pokey_stop()
note=0
freq=0
#define Draw_0
/*"/*'/**//* YYD ACTION
lib_id=1
action_id=603
applies_to=self
*/
i=0 repeat (9) {
    u=100+125*i
    draw_set_color(pick(type==i,$ffffff,$ff00))
    draw_rectangle(u,50,u+100,100,0)
    draw_set_color(0)
    draw_rectangle(u,50,u+100,100,1)
    draw_text_ext(u+5,50+5,pokey_get_instrument_name(i),-1,100)
i+=1}

draw_set_color($ffffff)
draw_rectangle(50,150,50+60*20,250,0)
draw_set_color(0)
draw_rectangle(50,150,50+60*20,250,1)

if (mode==1 and note) {
    u=50+(60*20)*note/254
    draw_set_color($ff00)
    draw_line_width(u,150,u,250,2)
}

o=0 repeat (5) {
    n=0 repeat (12) {
        u=50+(o*12+n)*20
        draw_set_color(pick(o*12+n==note-12 and mode==0,pick(n+4,$ffffff,0,$ffffff,0,$ffffff,0,$ffffff,$ffffff,0,$ffffff,0,$ffffff),$ff00))
        draw_rectangle(u,300,u+20,400,0)
        draw_set_color(0)
        draw_rectangle(u,300,u+20,400,1)
    n+=1}
o+=1}

draw_text(50,150-40,"#frequency: "+string(freq))
draw_text(50,300-40,"#note: "+string(note))
