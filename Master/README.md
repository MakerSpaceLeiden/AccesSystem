To run in test mode

1) optional - create environment.

Option - create a virtual environment to separate this from all other python code and library installs; and not needing any root powers:

   python3 -mvenv venv
   source venv/bin/activate

2) Ensure the requirements are installed:

   pip install -r requirements.txt

of als je niet in een, als bovenstaand, virtual 'venv' installeerd;

   sudo pip install -r requirements.txt

If you very much rely on macports, brew, or other packages that
puts things like gmp(.h) in non standard locations; then prefix
above PIP command with

   CFLAGS=-I/opt/local/include LDFLAGS=-L/opt/local/include pip install...

3) One off private seed reneration

Generate a private seed. You only need to do this once.

   python master.py \
		-N \
		--privatekeyfile seed.private

4) Reset TOFU (optional, just once)

Reset the trust-on-first-use key database if you want to not trust anything you have trusted in the past:

   true > tofu.db

5) Run the master

Consider running this in 'screen' if you are going to run it for a long time.

   python master.py \
		--privatekeyfile seed.private \
		--db ./sample-keydb.txt \
		--trustdb ./tofu.db \
		--config ./sample-acnode.ini \
		--debug \
		--no-mqtt-log \
		--ping 5 \
                --node master-$USER \
                --master master-$USER \
		--topic $USER

Note the use of --topic to get a 'personal' channel to test on!

