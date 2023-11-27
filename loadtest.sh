#!/usr/bin/env bash
#Command to install the tester:
# npm install -i loadtest

TEST_IP="192.168.2.131"
CONCURRENCY=1
CORES=1
TEST_TIME=60
LOG_FILE=psychic-http-loadtest.log

if test -f "$LOG_FILE"; then
  rm $LOG_FILE
fi

echo "Testing http://$TEST_IP/"
loadtest -c $CONCURRENCY --cores $CORES -t $TEST_TIME --timeout 5000 "http://$TEST_IP/" --quiet >> $LOG_FILE

echo "Testing http://$TEST_IP/api"
loadtest -c $CONCURRENCY --cores $CORES -t $TEST_TIME --timeout 5000 "http://$TEST_IP/api?foo=bar" --quiet >> $LOG_FILE

echo "Testing http://$TEST_IP/ws"
loadtest -c $CONCURRENCY --cores $CORES -t $TEST_TIME --timeout 5000 "ws://$TEST_IP/ws" --quiet 2> /dev/null >> $LOG_FILE

echo "Testing http://$TEST_IP/alien.png"
loadtest -c $CONCURRENCY --cores $CORES -t $TEST_TIME --timeout 5000 "http://$TEST_IP/alien.png" --quiet >> $LOG_FILE

#some basic test commands:
# curl http://psychic.local/
# curl http://psychic.local/404
# curl http://psychic.local/alien.png
# curl 'http://psychic.local/api?foo=bar'
# curl -d '{"foo":"bar"}' http://psychic.local/api
# curl -i -X POST -T src/PsychicHttp.cpp 'http://psychic.local/upload'
# curl -i -X POST -T loadtest.sh 'http://psychic.local/upload?_filename=loadtest.sh'
# curl -i -X POST -T loadtest.sh 'http://psychic.local/upload/loadtest.sh'
# curl --data-urlencode "param1=Value (One)" http://psychic.local/post