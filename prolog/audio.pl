%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
% This file is part of dres the resource policy dependency resolver.
% 
% Copyright (C) 2010 Nokia Corporation.
% 
% This library is free software; you can redistribute
% it and/or modify it under the terms of the GNU Lesser General Public
% License as published by the Free Software Foundation
% version 2.1 of the License.
% 
% This library is distributed in the hope that it will be useful,
% but WITHOUT ANY WARRANTY; without even the implied warranty of
% MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
% Lesser General Public License for more details.
% 
% You should have received a copy of the GNU Lesser General Public
% License along with this library; if not, write to the Free Software
% Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
% USA.
%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%

:- module(audio,
	  [set_routes/1, set_volume_limits/1, set_corks/1, playback_request/3,
	  available_accessory/2, privacy_override/1, active_policy_group/1,
	  current_route/2, privacy/1, connected/1]).

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
    Action=[audio_route, [type, SinkOrSource], [device, Device]].

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
    policy_group(sink, Group),
    group_volume_limit(Group, sink, NewLimit),
    Action=[volume_limit, [group, Group], [limit, NewLimit]].

set_volume_limits(ActionList) :-
    findall(A, volume_limit_action(A), ActionList).


/*
 * Cork
 */
cork_action(Action) :-
    policy_group(sink, PolicyGroup),
    current_route(sink, Device),
    (enforce_pausing(PolicyGroup, Device) *->
         Action=[cork_stream, [group, PolicyGroup], [cork, corked]]
     ;
         Action=[cork_stream, [group, PolicyGroup], [cork, uncorked]]
    ).

set_corks(ActionList) :-
    findall(A, cork_action(A), ActionList).


/*
 * Requests
 */
playback_request(PolicyGroup, MediaType, Grant) :-
    (reject_audio_play_request(PolicyGroup, MediaType) *->
         Grant=denied
     ;
         Grant=granted
    ).



/*
 * system state
 */

privacy_override(X) :-
    fact_exists('com.nokia.policy.privacy_override',
		[value], [X]).

connected(X) :-
    fact_exists('com.nokia.policy.accessories',
		[device, state], [X, '1']).

active_policy_group(X) :-
    fact_exists('com.nokia.policy.audio_active_policy_group',
		[group, state], [X, '1']).

current_route(SinkOrSource, Where) :-
    fact_exists('com.nokia.policy.audio.route',
		[type, device], [SinkOrSource, Where]).

current_volume_limit(PolicyGroup, Limit) :-
    fact_exists('com.nokia.policy.volume_limit',
		[group, limit], [PolicyGroup, Limit]).

current_cork(PolicyGroup, Corked) :-
    fact_exists('com.nokia.policy.audio.cork',
		[group, cork], [PolicyGroup, Corked]).


/*
privacy_override(X) :- related(privacy_override, [X]).

connected(X) :- related(connected, [X, _]).

active_policy_group(X) :- related(audio_active_policy_group, [X, _]).

current_route(SinkOrSource, Where) :-
    related(audio_route, [SinkOrSource, Where]).

current_volume_limit(PolicyGroup, Limit) :-
    related(volume_limit, [PolicyGroup, Limit]).

current_cork(PolicyGroup, Corked) :-
    related(audio_cork, [PolicyGroup, Corked]).
*/

/*
 * test predicates
 */

foo(ActionList) :- findall(X, bar(X), ActionList).
bar([audio_route, [type, source], [device, microphone]]).
bar([audio_route, [type, sink], [device, ihf]]).
