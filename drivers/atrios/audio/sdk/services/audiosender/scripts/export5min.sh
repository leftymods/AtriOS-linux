#!/bin/bash

set -e

TABLE=$1
DEST=//home/voice/vacert

./uniproxy-sessionlog-parser \
	--src //logs/alice-production-uniproxy/stream/5min/$TABLE \
	--dest $DEST/trash \
	--only-sync-state --dest-sync-state $DEST/$TABLE/sync_state --appid-sync-state audio_sender \
	--only-stream --dest-stream $DEST/$TABLE/stream --appid-stream audio_sender \
	--only-log-spotter --dest-log-spotter $DEST/$TABLE/log_spotter --appid-log-spotter audio_sender
