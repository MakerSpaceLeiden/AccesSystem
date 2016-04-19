#!/bin/sh

# PROVIDE: master
# REQUIRE: DAEMON
# BEFORE: LOGIN
# KEYWORD: shutdown

# Add the following lines to /etc/rc.conf to enable master:
#
# master_enable="YES"
# master_flags="<set as needed>"
#
# See master(8) for flags
#

. /etc/rc.subr

name=master
rcvar=master_enable

load_rc_config $name
: ${master_enable:="NO"}
: ${master_config:="/usr/local/etc/master/acnode.ini"}
: ${master_dbfile:="/usr/local/etc/master/keydb.txt"}
: ${master_user:="master"}
: ${master_pidfile:="/var/db/master/master.pid"}
: ${master_flags:=""}

pidfile=${master_pidfile}

command=/usr/local/AccesSystem/Master/master.py

command_args="-c ${master_config} --dbfile ${master_dbfile} ${master_flags} --pidfile ${master_pidfile} --daemonize"
required_files="${master_config} ${master_dbfile}"

extra_commands="reload"
stop_postcmd=stop_postcmd
stop_postcmd()
{
  rm -f $pidfile
}

load_rc_config $name
run_rc_command "$1"
