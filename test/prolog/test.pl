set_member(current_profile, general).

set_member(connected, ihf).
set_member(connected, earpiece).
set_member(connected, microphone).

set_member(active_policy_group, othermedia).

related(audio_route, [sink, earpiece]).
related(audio_route, [source, microphone]).

related(volume_limit, [cscall    , 100]).
related(volume_limit, [ringtone  , 100]).
related(volume_limit, [ipcall    , 100]).
related(volume_limit, [player    , 100]).
related(volume_limit, [fmradio   , 100]).
related(volume_limit, [othermedia, 100]).
