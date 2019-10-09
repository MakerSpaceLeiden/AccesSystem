#!/usr/bin/env python3.4
#
import sys
import uuid

import smtplib
import time
import logging
from email.mime.text import MIMEText

from ACNode import ACNode

class AlertEmail(ACNode):
  default_smtphost = 'localhost'
  default_smtpport = 25
  default_alertsubject = "[Node alert]"
  default_alertfrom = 'acnode@unknown'

  # Firstly - we have an 'offline' mode that allows for
  # testing without the hardware (i.e. on any laptop or
  # machine with python); without the need for the stepper
  # motor, mosfet or RFID reader.
  #
  def parseArguments(self):
    self.parser.add('--smtphost', action='store', default= self.default_smtphost,
                   help='SMTP host (default is '+self.default_smtphost+').')

    self.parser.add('--smtpuser', action='store',
                   help='SMTP username (default is none)')
    self.parser.add('--smtppasswd', action='store',
                   help='SMTP password (default is none)')

    self.parser.add('--smtpport', action='store', default=self.default_smtpport,
                   help='SMTP host (default is '+str(self.default_smtpport)+').')

    self.parser.add('--alertsubject', action='store', default=self.default_alertsubject,
                   help='Subject prefix alert emails (default is '+self.default_alertsubject+').')

    self.parser.add('--alertfrom', action='store', default=self.default_alertfrom,
                   help='Sender of alert emails (default is '+self.default_alertfrom+').')
    self.parser.add('--alertto', action='append', 
                   help='Sender of alert emails (default is none). May be used multiple times.')

    super().parseArguments()

  def send_email(self,mailmsg,mailsubject, dst = None):
    if not self.cnf.alertto:
        self.logger.debug("No alert email sent - not configured.")
        return
 
    to = self.cnf.alertto
    if dst:
        to = dst

    COMMASPACE = ', '

    s = smtplib.SMTP(self.cnf.smtphost, self.cnf.smtpport)
    if self.cnf.smtpuser and self.cnf.smtppasswd:
       s.login(self.cnf.smtpuser, self.cnf.smtppasswd)

    # s.sendmail(self.cnf.alertfrom, self.cnf.alertto, msg.as_string())
    for st in to:
       msg = MIMEText(mailmsg)

       msg['Subject'] = self.cnf.alertsubject + ' ' + mailsubject 
       msg['From'] = 'ACNode ' + self.cnf.node + ' <' + self.cnf.alertfrom + '>'
       msg['Message-ID'] = "{}-{}".format(str(uuid.uuid1()), self.cnf.alertfrom)
       msg['To'] = st
       s.send_message(msg) 

    s.quit()

if __name__ == "__main__":
  acnode = AlertEmails()
  if not acnode:
    sys.exit(1)

  acnode.parseArguments()
  acnode.setup()

  if not acnode.cnf.alertto:
     print("You did not specify an email destination. aborting test.")
     sys.exit(1)

  acnode.send_email("This is a test.", "test")


