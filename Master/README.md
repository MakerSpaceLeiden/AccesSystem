Run in test mode with:

    python3.6 master.py --privatekeyfile .priv --db ./sample-keydb.txt --config ./sample-acnode.ini --debug --no-mqtt-log --ping 5 --topic $USER

Note the use of --topic to get a 'personal' channel to test on!


pip3.6 install daemon setproctitle paho-mqtt axolotl_curve25519 axolotl python-axolotl-curve25519 ed25519 Crypto pycrypto
