#!/bin/ksh
######################################################################
# Program:	pianostatuser
# Purpose:	Monitors piano and updates IM and other statuses
#		with the current track.
# Author:	Perette Barella
#---------------------------------------------------------------------


######################################################################
# Function:	usage
# Purpose:	Displays the usage of this command.
# Author:	Perette Barella
#---------------------------------------------------------------------
function usage
{
	echo "Usage: $arg0 [-p port] [-h host] <pianod command>"
	echo "   -h host   : Use pianod server on host."
	echo "   -p port   : Connect on port instead of 4445."
	exit 1
}
##### End of function usage #####






######################################################################
# Function:	parse_arguments
# Purpose:	Parses the command line arguments
# Arguments:	The command line arguments.
# Returns:	Number of arguments parsed.
# Author:	Perette Barella
#---------------------------------------------------------------------
function parse_arguments
{
	typeset option
	while getopts 'p:h:' option
	do
		case "$option" in
			p)	PORT="$OPTARG" ;;
			h)	HOST="$OPTARG" ;;
			*)	usage ;;
		esac
	done
	typeset status=0
	return $((OPTIND - 1))
}
##### End of function parse_arguments #####



######################################################################
# Function:	update_status
# Purpose:	Updates status messages of various messengers
# Arguments:	The status message.
# Author:	Perette Barella
#---------------------------------------------------------------------
function update_status
{
	typeset status="$1"
	if killall -0 Adium 2>/dev/null
	then
		osascript << EOF
tell application "Adium"
	go available with message "$status (pianod)"
end tell
EOF
	fi

	if killall -0 Skype 2>/dev/null
	then
		osascript >/dev/null 2>&1 << EOF
tell application "Skype"
	send command "SET PROFILE MOOD_TEXT $status (pianod)" script name "IMStatus"
end tell
EOF
	fi

	if killall -0 iChat 2>/dev/null
	then
		osascript << EOF
tell application "iChat"
	set status message to "$status (pianod)"
end tell
EOF
	fi

	if [ "$(whence growlnotify)" != "" ]
	then
		growlnotify -m "$status" -p low "pianod"
	fi

}
##### End of function update_status #####




##### Start of main #####

arg0=$(basename $0)

HOST="${PIANOD_HOST:-localhost}"
PORT="${PIANOD_PORT:-4445}"
parse_arguments "$@"
shift $?


# Read the socket until it closes, gathering track and artist data.
nc -d "$HOST" "$PORT" | while read status title message
do
	if [ "$status" = "113" ]
	then
		artist="$message"
	elif [ "$status" = "114" ]
	then
		update_status "$message by $artist"
	fi
done


