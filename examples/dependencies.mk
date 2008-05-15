PREFIX = com.nokia.policy

$accessories  = { device: ihf, state: 1 }
$accessories += { device: earpiece, state: 1 }
$accessories += { device: microphone, state: 1 }

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

foo:
	$audio_playback = prolog(foo, &foo=bar)

#
#wlancheck:
#	&netdev[type:WLAN].status = prolog(wlancheck, \
#	                                   $wlandevice[type: WLAN, status: 1])

