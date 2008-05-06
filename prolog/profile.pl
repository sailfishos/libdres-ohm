profile(general, privacy, public).

profile(silent, privacy, private).

profile(meeting, privacy, private).

profile(outdoor, privacy, public).

current_profile(X) :-
    related(current_profile, [X]).
