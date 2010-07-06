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

audio_device_type(source).
audio_device_type(sink).

audio_device(sink  , private, ihfandheadset).
audio_device(sink  , private, bluetooth).
audio_device(sink  , private, headset).
audio_device(sink,   private, headphone).
audio_device(sink  , private, earpiece).
audio_device(sink  , public , ihf).
audio_device(source, private, bluetooth).
audio_device(source, private, headset).
audio_device(source, private, headmike).
audio_device(source, private, microphone).

accessory(ihfandheadset).
accessory(headset).
accessory(bluetooth).
accessory(headphone).
accessory(headmike).
