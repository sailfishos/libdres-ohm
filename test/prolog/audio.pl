:- module(audio,
	  [set_routes/1, set_volume_limits/1, set_corks/1, playback_request/2,
	  available_accessory/2, privacy_override/1, active_policy_group/1,
	  current_route/2, privacy/1]).

/*
 * Generic rules
 */
head([Elem|_], Elem).


/*
 * Audio Devices
 */
available_device(SinkOrSource, Privacy, Device) :-
    audio_device(SinkOrSource, Privacy, Device),
    connected(Device).
    
available_accessory(SinkOrSource, Device) :-
    accessory(Device),
    connected(Device),
    audio_device(SinkOrSource, _, Device).


/*
 * Privacy
 */
privacy_map(private, private).
privacy_map(public , public).
privacy_map(public , private).

privacy(Privacy) :-
    (privacy_override(P) *->
         privacy_map(P, Privacy)
     ;
         current_profile(Profile),
         profile(Profile, privacy, P),
         privacy_map(P, Privacy)
    ).


/*
 * routing 
 */
device_candidate(SinkOrSource, Device) :-
    privacy(Privacy),
    available_device(SinkOrSource, Privacy, Device),
    policy_group(SinkOrSource, Group),
    active_policy_group(Group),
    not(invalid_audio_device_choice(Group, SinkOrSource, Privacy, Device)).

device_candidate_list(SinkOrSource, DeviceList) :-
    findall(S, device_candidate(SinkOrSource, S), DeviceList).

route_to_device(SinkOrSource, Device) :-
    device_candidate_list(SinkOrSource, DeviceList),
    (current_profile(Profile),
     profile(Profile, device, PreferredDevice),
     member(PreferredDevice, DeviceList) *->
         Device = PreferredDevice
     ;
         head(DeviceList, Device)
    ).

route_action(Action) :-
    audio_device_type(SinkOrSource),
    route_to_device(SinkOrSource, Device),
    Action=[audio_route, SinkOrSource, Device].

set_routes(ActionList) :-
    findall(A, route_action(A), ActionList).




/*
 * Volume limit
 */
group_volume_limit_candidate(GroupToLimit, SinkOrSource, Limit) :-
    (policy_group(SinkOrSource, Group),
     (active_policy_group(Group) *->
          volume_limit(Group, SinkOrSource, GroupToLimit, Limit)
      ;
          Limit=100
     )
    );
    volume_limit_exception(GroupToLimit, SinkOrSource, Limit).

group_volume_limit(Group, SinkOrSource, Limit) :-
    findall(L, group_volume_limit_candidate(Group, SinkOrSource, L),
	    LimitList),
    sort(LimitList, SortedLimitList),
    head(SortedLimitList, Limit).

volume_limit_action(Action) :-
    policy_group(SinkOrSource, Group),
    group_volume_limit(Group, SinkOrSource, NewLimit),
    Action=[volume_limit, Group, SinkOrSource, NewLimit].

set_volume_limits(ActionList) :-
    findall(A, volume_limit_action(A), ActionList).


/*
 * Cork
 */
cork_action(Action) :-
    policy_group(sink, PolicyGroup),
    current_route(sink, Device),
    (enforce_pausing(PolicyGroup, Device) *->
         Action=[cork_stream, PolicyGroup, corked]
     ;
         Action=[cork_stream, PolicyGroup, uncorked]
    ).

set_corks(ActionList) :-
    findall(A, cork_action(A), ActionList).


/*
 * Requests
 */
playback_request(PolicyGroup, MediaType) :-
    not(reject_audio_play_request(PolicyGroup, MediaType)).



/*
 * system state
 */
privacy_override(X) :- set_member(privacy, X).

connected(X) :- set_member(connected, X).

active_policy_group(X) :- set_member(active_policy_group, X).

current_route(SinkOrSource, Where) :-
    related(audio_route, [SinkOrSource, Where]).

current_volume_limit(PolicyGroup, Limit) :-
    related(volume_limit, [PolicyGroup, Limit]).

current_cork(PolicyGroup, Corked) :-
    related(audio_cork, [policyGroup, Corked]).
