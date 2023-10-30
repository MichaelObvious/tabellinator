set -xe
emcc tabellinator.c -s TOTAL_MEMORY=150MB -s EXPORTED_RUNTIME_METHODS=["cwrap"] --bind