PREFIX = com.nokia.policy

# accessories
$accessories  = { device: ihf       , state: 1 }
$accessories += { device: earpiece  , state: 1 }
$accessories += { device: microphone, state: 1 }
$accessories += { device: bluetooth , state: 0 }
$accessories += { device: headset   , state: 0 }
$accessories += { device: headphone , state: 0 }
$accessories += { device: headmike  , state: 0 }

# audio rouring
$audio_route  = { type: source, device: microphone }
$audio_route += { type: sink  , device: ihf        }

# active policy groups

$audio_active_policy_group  = { group: othermedia, state: 1 }
$audio_active_policy_group += { group: fmradio   , state: 0 }
$audio_active_policy_group += { group: player    , state: 0 }
$audio_active_policy_group += { group: ipcall    , state: 0 }
$audio_active_policy_group += { group: cscall    , state: 0 }
$audio_active_policy_group += { group: ringtone  , state: 0 }

# audio corking
$audio_cork  = { group: othermedia, cork: uncorked }
$audio_cork += { group: fmradio   , cork: uncorked }
$audio_cork += { group: player    , cork: uncorked }
$audio_cork += { group: ipcall    , cork: uncorked }
$audio_cork += { group: cscall    , cork: uncorked }
$audio_cork += { group: ringtone  , cork: uncorked }

# volume limits
$volume_limit  = { group: othermedia, limit: 100 }
$volume_limit += { group: fmradio   , limit: 100 }
$volume_limit += { group: player    , limit: 100 }
$volume_limit += { group: ipcall    , limit: 100 }
$volume_limit += { group: cscall    , limit: 100 }
$volume_limit += { group: ringtone  , limit: 100 }

$audio_playback_request = { value: }
$audio_playback         = { value: }
$cpu_load               = { value: }
$privacy_override       = { value: default }
$current_profile        = { value: general }
$temperature            = { value: }
$cpu_frequency          = { value: }
$max_cpu_frequency      = { value: }
$min_cpu_frequency      = { value: }
$idle                   = { value: }
$battery                = { value: }
$sleeping_state         = { value: }
$sleeping_request       = { value: }


all: sleeping_state cpu_frequency audio_route audio_volume_limit


sleeping_state: $sleeping_request $battery $idle
	$sleeping_state = prolog(set_sleeping_state, &sleeping_request, \
                                 $battery, $idle)

cpu_frequency: sleeping_state $min_cpu_frequency $max_cpu_frequency \
			$battery $temperature
	$cpu_frequency[type:arm] = prolog(set_cpu_frequency, &min_cpu_frequency, &max_cpu_frequency)

audio_route: $current_profile $privacy_override $accessories \
             $audio_active_policy_group
	$audio_route = prolog(set_routes)

audio_volume_limit: audio_route $audio_active_policy_group
	$volume_limit = prolog(set_volume_limits)

audio_cork: audio_route $audio_active_policy_group
	$audio_cork = prolog(set_corks)

audio_playback: cpu_frequency audio_route audio_volume_limit audio_cork \
                $cpu_load $audio_playback_request
	prolog(cpu_frequency_check, min, 300)
	prolog(cpu_load_check, max, 40)
	$audio_playback = prolog(playback_request, \
                                 &audio_playback_request.policy_group, \
                                 &audio_playback_request.media)

audio_playback_request:
	dres(audio_playback, \
             &audio_playback_request = play, \
             &min_cpu_frequency = 300)

#
#wlancheck:
#	&netdev[type:WLAN].status = prolog(wlancheck, \
#	                                   $wlandevice[type: WLAN, status: 1])

