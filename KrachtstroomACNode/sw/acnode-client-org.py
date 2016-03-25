#!/usr/bin/env python

import time
import json

import paho.mqtt.client as mqtt
import paho.mqtt.publish as publish

import smtplib
import time
import logging
from email.mime.text import MIMEText 

DEFAULT_CC=['mvn@martenvijn.nl']
client = mqtt.Client()

def send_email(mailmsg,mailsubject, to=None):
	# Imenform of actions 
	# Send to log by default
	sendto = DEFAULT_CC
	# Include extra person if provided
	if to: sendto.append(to)

	sendfrom  = 'spacelog@makerspaceleiden.nl'
	msg = MIMEText(mailmsg)
	msg['Subject'] = mailsubject 
	msg['From'] = sendfrom
	msg['To'] = ','.join(sendto)
	s = smtplib.SMTP('smtp.xs4all.nl')
	s.sendmail(sendfrom, sendto, msg.as_string())
	s.quit()

               #    publish.single("deur/test/open", "open", hostname="192.168.5.6", protocol="publish.MQTTv311")
#       		   send_email(mailmsg,mailsubject)
#                   publish.single("makerspace/deur/space", name, hostname="192.168.5.20", protocol="publish.MQTTv311",retain=True)

def on_message(client, userdata, message):
    print message.payload
    power=message.payload
#   if	 

client.connect("space.makerspaceleiden.nl")
client.subscribe("makerspace/wlan/ap1/power",1)


client.on_message = on_message


while ( True ):
	client.loop()
#!/usr/bin/env python

import paho.mqtt.client as mqtt
import time
import paho.mqtt.publish as publish


import smtplib
import time
import logging
from email.mime.text import MIMEText 

dbfile='/root/keydb.txt'

deur="space"
mailsubject="-"
logging.basicConfig(filename='/var/log/deur.log',level=logging.DEBUG)
logging.debug('This message should go to the log file')
logging.info('So should this')
logging.warning('And this, too')
logger = logging.getLogger() 


#DEFAULT_CC=['spacelog@makerspaceleiden.nl']
DEFAULT_CC=['mvn@martenvijn.nl']
ACCESS_UKNOWN, ACCESS_DENIED, ACCESS_GRANTED = ('UNKNOWN', 'DENIED', 'GRANTED')

client = mqtt.Client()

def send_email(mailmsg,mailsubject, to=None):
	# Imenform of actions 
	# Send to log by default
	sendto = DEFAULT_CC
	# Include extra person if provided
	if to: sendto.append(to)

	sendfrom  = 'spacelog@makerspaceleiden.nl'
	msg = MIMEText(mailmsg)
	msg['Subject'] = mailsubject 
	msg['From'] = sendfrom
	msg['To'] = ','.join(sendto)
	s = smtplib.SMTP('smtp.xs4all.nl')
	s.sendmail(sendfrom, sendto, msg.as_string())
	s.quit()

def send_email_admin(adminmsg, to=None):
	sendfrom  = 'spacelog@makerspaceleiden.nl'
	admin = 'mc@nerdia.nl'
	msg = MIMEText(adminmsg)
	msg['Subject'] = 'ERROR msg from space-pi' 
	msg['From'] = sendfrom
	msg['To'] = admin 
	s = smtplib.SMTP('smtp.xs4all.nl')
	s.sendmail(sendfrom, admin, msg.as_string())
	s.quit()


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
       dbmsg='%s:%s:%s:%s' % (deur, tag, name, access)
       mailmsg='name: %s access: %s' % (name, access)
       mailsubject='%s-deur  %s %s' % (deur, access, name)
       logger.info(spacemsg)

       # Try to see if user exists and is allowed
       if tag in userdb:
		if userdb[tag]['access'] == ACCESS_GRANTED:
                   publish.single("deur/test/open", "open", hostname="192.168.5.6", protocol="publish.MQTTv311")
#       		   send_email(mailmsg,mailsubject)
#                   publish.single("makerspace/deur/space", name, hostname="192.168.5.20", protocol="publish.MQTTv311",retain=True)
		   logging.info(spacemsg)
		else:
		   print userdb[tag]
		   logging.info("invalid access key '%s'", userdb[tag])
       		   adminmsg=spacemsg
		   send_email_admin(adminmsg)
       else:
	 	adminmsg=spacemsg
		logging.info("unknown key '%s'", spacemsg )
#       		send_email_admin(adminmsg)
#       publish.single("deur/access", dbmsg, hostname="192.168.4.2", protocol="publish.MQTTv311")

def on_message(client, userdata, message):
    print message.payload
    tag=message.payload 
    process_tag(userdb, tag)
#        + message.topic + "' with QoS " + str(message.qos))


try:
  fd = open('config.json', 'r')
  cnf = json.load(fd)
  cnf.dump();
except:
  print "Failed to load configuration file. Aborting."
  raise


client.connect("192.168.5.6")
client.subscribe("deur/test/rfid",1)

client.on_message = on_message


userdb = parse_db(dbfile)

logger.debug(userdb)
	

while ( True ):
	client.loop()

