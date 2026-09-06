import {focusedAppId} from '../contrib/gnome/spacenav-focus@jl1990/focus.js';
function equal(a,b) { if(a !== b) throw Error(`${a} != ${b}`); }
const win = {get_wm_class: () => 'fallback'};
const tracker = {get_window_app: () => ({get_id: () => 'org.blender.Blender.desktop'})};
equal(focusedAppId(win, tracker, false), 'org.blender.Blender.desktop');
equal(focusedAppId(win, tracker, true), '');
equal(focusedAppId(null, tracker, false), '');
equal(focusedAppId(win, {get_window_app:()=>null}, false), 'fallback');
equal(focusedAppId(win, {get_window_app:()=>({get_id:()=> 'bad\nname'})}, false), '');
equal(focusedAppId(win, {get_window_app:()=>({get_id:()=> 'x'.repeat(256)})}, false), '');
print('GNOME focus identity tests passed');
