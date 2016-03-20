#!/usr/bin/env python

import paho.mqtt.client as mqtt
import time
import paho.mqtt.publish as publish
import logging

sys.path.append('../../lib')
import configRead
import alertEmail

configRead.load()
cnf = configRead.cnf

node=cnf['node']
mailsubject="-"

logging.basicConfig(filename='/var/log/" + node + ".log',level=logging.DEBUG)
logger = logging.getLogger() 

ACCESS_UKNOWN, ACCESS_DENIED, ACCESS_GRANTED = ('UNKNOWN', 'DENIED', 'GRANTED')

client = mqtt.Client()

def send_email_admin(adminmsg)
        alertEmailSansCC(adminmsg, 'ERROR msg from space-pi', cnf['alert_email_admin'])

def parse_db(dbfile):
	userdb = {}
        try:
           for row in open(dbfile,'r'):
		if row.startswith('#'): continue
		if len(row.split(':')) != 4: continue
		
		# Parse keys to valid input
		tag,access,name,email = row.strip().split(':')
		
		# Build static ACL listing
		if access == 'ok':
			acl = ACCESS_GRANTED
		elif access == 'no':
			acl = ACCESS_DENIED
		else:
			acl = ACCESS_UKNOWN
			logger.info("Unable to parse access=%s", access)
		# Create database
		userdb[tag] = { 'tag': 'hidden', 'access': acl, 'name': name, 'email': email }
           except IOError as e:
             print "I/O error({0}): {1}".format(e.errno, e.strerror)
             raise
           except ValueError:
             print "Could not convert data to an integer -- some malformed tag ?"
             raise
           except:
             print "Unexpected error:", sys.exc_info()[0]
             raise

	return userdb

def process_tag(userdb, tag):
 	# Variables for email purposes
       email = userdb[tag]['email'] if tag in userdb else 'none'
       name = userdb[tag]['name'] if tag in userdb else 'none'
       access = userdb[tag]['access'] if tag in userdb else ACCESS_UKNOWN
	
	# Notify Client
       spacemsg='tag: %s name: %s email: %s access: %s' % (tag, name, email, access)
       dbmsg='%s:%s:%s:%s' % (node, tag, name, access)
       mailmsg='name: %s access: %s' % (name, access)
       mailsubject='%s  %s %s' % (node, access, name)
       logger.info(spacemsg)

       # Try to see if user exists and is allowed
       if tag in userdb:
		if userdb[tag]['access'] == ACCESS_GRANTED:
                   publish.single(node + "/test/open", "open", hostname="192.168.5.6", protocol="publish.MQTTv311")
		   logging.info(spacemsg)
		else:
		   print userdb[tag]
		   logging.info("invalid access key '%s'", userdb[tag])
       		   adminmsg=spacemsg
		   send_email_admin(adminmsg)
       else:
	 	adminmsg=spacemsg
		logging.info("unknown key '%s'", spacemsg )

def on_message(client, userdata, message):
    print message.payload
    tag=message.payload 
    process_tag(userdb, tag)

client.connect(cnf['mqtt']['host'])
client.subscribe(cnf['mqtt']['sub'],1)
client.on_message = on_message

userdb = parse_db(cnf['dbfile'])

logger.debug(userdb)
	

while ( True ):
	client.loop()
