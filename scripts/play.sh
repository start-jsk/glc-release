#!/bin/bash
#
# play.sh -- playing glc stream with mplayer
# Copyright (C) 2007 Pyry Haulos
# For conditions of distribution and use, see copyright notice in glc.h

CTX="1"
AUDIO="1"

AUDIOFIFO="/tmp/glc-audio.fifo"
VIDEOFIFO="/tmp/glc-video.fifo"

if [ "$1" == "" ]; then
	echo "$0 FILE [ctx] [audio]"
	exit 1
fi

[ "$2" != "" ] && CTX=$2
[ "$3" != "" ] && AUDIO=$3

mkfifo "${AUDIOFIFO}"
mkfifo "${VIDEOFIFO}"

glc-play "$1" -o "${AUDIOFIFO}" -a "${AUDIO}" &
glc-play "$1" -o "${VIDEOFIFO}" -y "${CTX}" &

mplayer -audio-demuxer lavf -demuxer y4m -audiofile "${AUDIOFIFO}" "${VIDEOFIFO}"

rm -f "${AUDIOFIFO}" "${VIDEOFIFO}"
