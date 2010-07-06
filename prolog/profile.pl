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

profile(general, privacy, public).

profile(silent, privacy, private).

profile(meeting, privacy, private).

profile(outdoor, privacy, public).

current_profile(X) :-
    fact_exists('com.nokia.policy.current_profile', [value], [X]).

/*
current_profile(X) :-
    related(current_profile, [X]).
*/
