#!/bin/sh -e

exec /usr/local/bin/tauriond \
  --datadir="${XAYAGAME_DIR}" \
  --enable_pruning=1000 \
  --game_rpc_port=8600 \
  --rest_port=8700 \
  "$@"
